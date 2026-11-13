"""Wire protocol for the Pico Constellation ADC recorder.

Firmware frame format (little-endian):

    magic                   uint16  0x5AA5
    seq                     uint16  Frame sequence number
    first_sample_index      uint32  Index of first sample
    count                   uint16  Number of samples (512)
    dropped_since_last      uint32  Samples dropped since previous frame
    samples                 uint16[count] Raw ADC samples
    crc                     uint16  CRC-16/CCITT-FALSE

Header: 14 bytes
Payload: 1024 bytes
Footer: 2 bytes
Full frame: 1040 bytes

The parser:
    - Resynchronizes using the magic word.
    - Validates CRC-16/CCITT-FALSE.
    - Detects missing frames using sequence numbers.
    - Detects missing samples using sample indices.
    - Tracks firmware-reported dropped samples.
    - Handles sequence and sample-index rollover.

Samples are handled as raw uint16 little-endian data, compatible with:

    np.fromfile(FILENAME, dtype=np.uint16)
"""

import struct

MAGIC = 0x5AA5

HEADER_FORMAT = struct.Struct("<HHIHI")
HEADER_SIZE = HEADER_FORMAT.size  # 14 bytes

FOOTER_SIZE = 2

FRAME_SAMPLES = 512
MAX_FRAME_SAMPLES = FRAME_SAMPLES

MAX_FRAME_SIZE = HEADER_SIZE + (MAX_FRAME_SAMPLES * 2) + FOOTER_SIZE

READ_SIZE = 4096


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)."""

    crc = 0xFFFF

    for byte in data:
        crc ^= byte << 8

        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF

    return crc


class FrameParser:
    """
    Incremental binary frame parser.

    feed(data) returns a list of:

        (seq, first_sample_index, dropped_since_last, sample_bytes)

    sample_bytes are already little-endian uint16 data.

    Statistics:
        frames_ok
        frames_bad_crc
        bad_headers
        resyncs
        bytes_discarded
        seq_gaps
        frames_missing
        reported_drops
        sample_gaps
        samples_missing
        sample_index_resets
        dropped_counter_mismatches
    """

    def __init__(self):
        self._buf = bytearray()
        self._expect_seq = None
        self._expect_sample_index = None

        self.stats = {
            "frames_ok": 0,
            "frames_bad_crc": 0,
            "bad_headers": 0,
            "resyncs": 0,
            "bytes_discarded": 0,
            "seq_gaps": 0,
            "frames_missing": 0,
            "reported_drops": 0,
            "sample_gaps": 0,
            "samples_missing": 0,
            "sample_index_resets": 0,
            "dropped_counter_mismatches": 0,
        }

    def feed(self, data: bytes):
        """Feed arbitrary chunks of serial data and return valid frames."""

        self._buf.extend(data)

        frames = []
        buf = self._buf
        magic = struct.pack("<H", MAGIC)

        while True:

            # -------------------------------------------------------------
            # Find frame synchronization word
            # -------------------------------------------------------------

            idx = buf.find(magic)

            if idx < 0:
                # Preserve the last byte in case it starts the magic word.
                discard = max(0, len(buf) - 1)

                if discard:
                    del buf[:discard]

                    self.stats["resyncs"] += 1
                    self.stats["bytes_discarded"] += discard

                break

            if idx > 0:
                del buf[:idx]

                self.stats["resyncs"] += 1
                self.stats["bytes_discarded"] += idx

            # -------------------------------------------------------------
            # Wait for complete header
            # -------------------------------------------------------------

            if len(buf) < HEADER_SIZE:
                break

            (
                magic_rx,
                seq,
                first_sample_index,
                count,
                dropped_since_last,
            ) = HEADER_FORMAT.unpack_from(buf)

            if magic_rx != MAGIC or count != FRAME_SAMPLES:
                # Invalid header. Advance one byte and search again.
                del buf[0]

                self.stats["bad_headers"] += 1
                self.stats["resyncs"] += 1
                self.stats["bytes_discarded"] += 1

                continue

            # -------------------------------------------------------------
            # Wait for complete frame
            # -------------------------------------------------------------

            frame_len = HEADER_SIZE + (count * 2) + FOOTER_SIZE

            if len(buf) < frame_len:
                break

            # -------------------------------------------------------------
            # Validate CRC
            # -------------------------------------------------------------

            frame = bytes(buf[:frame_len])

            payload = frame[:-FOOTER_SIZE]

            crc_rx = struct.unpack_from(
                "<H",
                frame,
                frame_len - FOOTER_SIZE
            )[0]

            crc_calculated = crc16_ccitt(payload)

            if crc_calculated != crc_rx:

                self.stats["frames_bad_crc"] += 1

                # Only discard one byte. There may be another valid
                # synchronization word inside the corrupted candidate.
                del buf[0]

                self.stats["resyncs"] += 1
                self.stats["bytes_discarded"] += 1

                continue

            # -------------------------------------------------------------
            # Extract sample payload
            # -------------------------------------------------------------

            sample_start = HEADER_SIZE
            sample_end = sample_start + (count * 2)

            samples = bytes(frame[sample_start:sample_end])

            # Frame is valid. Remove it from the receive buffer.
            del buf[:frame_len]

            # -------------------------------------------------------------
            # Track frame sequence gaps
            # -------------------------------------------------------------

            if self._expect_seq is not None:

                expected = self._expect_seq

                if seq != expected:

                    missing = (seq - expected) & 0xFFFF

                    self.stats["seq_gaps"] += missing
                    self.stats["frames_missing"] += missing

            # -------------------------------------------------------------
            # Track sample discontinuities
            # -------------------------------------------------------------

            if self._expect_sample_index is not None:

                expected = self._expect_sample_index

                missing = (
                    first_sample_index - expected
                ) & 0xFFFFFFFF

                if 0 < missing < 0x80000000:

                    self.stats["sample_gaps"] += 1
                    self.stats["samples_missing"] += missing

                elif missing >= 0x80000000:

                    # Sample index moved backwards or recorder restarted.
                    self.stats["sample_index_resets"] += 1

            # -------------------------------------------------------------
            # Verify firmware dropped-sample counter
            # -------------------------------------------------------------

            self.stats["reported_drops"] += dropped_since_last

            # When frame sequence numbers are continuous, the sample
            # index gap should equal the firmware-reported drop count.
            #
            # If a frame was lost in transport, these values can differ.
            if (
                self._expect_sample_index is not None
                and self._expect_seq is not None
                and seq == self._expect_seq
            ):

                expected_gap = (
                    first_sample_index - self._expect_sample_index
                ) & 0xFFFFFFFF

                if expected_gap != dropped_since_last:
                    self.stats["dropped_counter_mismatches"] += 1

            # Update expected values.
            self._expect_seq = (seq + 1) & 0xFFFF

            self._expect_sample_index = (
                first_sample_index + count
            ) & 0xFFFFFFFF

            self.stats["frames_ok"] += 1

            frames.append((
                seq,
                first_sample_index,
                dropped_since_last,
                samples,
            ))

        return frames
