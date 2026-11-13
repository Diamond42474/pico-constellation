
#!/usr/bin/env python3
"""
Record raw ADC samples streamed from a Pico Constellation recorder.

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

Samples are saved as raw uint16 little-endian data, compatible with:

    np.fromfile(FILENAME, dtype=np.uint16)

Usage:
    python record.py --port COM5
    python record.py --port /dev/ttyACM0 --duration 30 --out capture.raw
    python record.py --port COM5 --expected-rate 44100
"""

import argparse
import struct
import sys
import threading
import time

MAGIC = 0x5AA5

HEADER_FORMAT = struct.Struct("<HHIHI")
HEADER_SIZE = HEADER_FORMAT.size  # 14 bytes

FOOTER_SIZE = 2

FRAME_SAMPLES = 512
MAX_FRAME_SAMPLES = FRAME_SAMPLES

MAX_FRAME_SIZE = HEADER_SIZE + (MAX_FRAME_SAMPLES * 2) + FOOTER_SIZE

READ_SIZE = 4096
SINK_FLUSH_SIZE = 65536


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


def open_serial(port: str):
    """Open the serial port, waiting for the device to appear."""

    import serial

    print(f"Opening {port} ...")

    while True:
        try:
            ser = serial.Serial(
                port=port,
                baudrate=115200,
                timeout=1.0,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False,
            )

            print(f"Connected to {port}")
            return ser

        except (serial.SerialException, OSError) as e:

            print(f"Could not open {port}: {e}")
            print("Is the Pico plugged in? Retrying every 2 seconds.")

            time.sleep(2.0)


def main():

    ap = argparse.ArgumentParser(
        description="Record ADC samples from a Pico Constellation recorder."
    )

    ap.add_argument(
        "--port",
        required=True,
        help="Serial port (e.g. COM5 or /dev/ttyACM0)"
    )

    ap.add_argument(
        "--out",
        default="capture.raw",
        help="Output raw file. Default: capture.raw"
    )

    ap.add_argument(
        "--duration",
        type=float,
        default=None,
        help="Recording duration in seconds."
    )

    ap.add_argument(
        "--expected-rate",
        type=float,
        default=44100.0,
        help="Nominal ADC sample rate. Default: 44100 Hz."
    )

    args = ap.parse_args()

    if args.duration is not None and args.duration <= 0:
        ap.error("--duration must be greater than zero")

    if args.expected_rate <= 0:
        ap.error("--expected-rate must be greater than zero")

    try:
        ser = open_serial(args.port)

    except KeyboardInterrupt:
        print("\nAborted before connecting.")
        return 1

    parser = FrameParser()

    stop = threading.Event()
    errors = []

    # Reader thread appends validated little-endian sample bytes here.
    sink = bytearray()
    sink_lock = threading.Lock()

    # Total validated samples received from the Pico.
    total_received = 0

    # Samples successfully written to the output file.
    samples_written = 0

    out_file = open(args.out, "wb")

    # ---------------------------------------------------------------------
    # Serial reader thread
    # ---------------------------------------------------------------------

    def reader():

        nonlocal total_received

        empty_streak = 0

        try:

            while not stop.is_set():

                data = ser.read(READ_SIZE)

                if not data:

                    empty_streak += 1

                    if empty_streak >= 5:
                        errors.append(
                            "No data for approximately 5 seconds "
                            "(device disconnected or recording stopped)."
                        )
                        break

                    continue

                empty_streak = 0

                for (
                    _seq,
                    _first_sample_index,
                    _dropped_since_last,
                    samples,
                ) in parser.feed(data):

                    sample_count = len(samples) // 2

                    with sink_lock:
                        sink.extend(samples)

                    total_received += sample_count

        except Exception as e:

            errors.append(str(e))
            stop.set()

    t = threading.Thread(target=reader, daemon=True)

    t.start()

    start = time.monotonic()

    print(
        f"Recording to {args.out} "
        f"(expected ~{args.expected_rate:.0f} Hz). "
        "Press Ctrl+C to stop."
    )

    # ---------------------------------------------------------------------
    # Main recording loop
    # ---------------------------------------------------------------------

    try:

        last_report = start

        while not stop.is_set():

            now = time.monotonic()

            if (
                args.duration is not None
                and now - start >= args.duration
            ):
                break

            # Flush buffered sample data to disk.
            with sink_lock:

                if len(sink) >= SINK_FLUSH_SIZE:

                    out_file.write(sink)

                    samples_written += len(sink) // 2

                    sink.clear()

            # ---------------------------------------------------------
            # Periodic status report
            # ---------------------------------------------------------

            now = time.monotonic()

            if now - last_report >= 1.0:

                elapsed = now - start

                measured_rate = (
                    total_received / elapsed
                    if elapsed > 0
                    else 0.0
                )

                s = parser.stats

                print(
                    f"  {measured_rate:8.0f} Hz   "
                    f"samples {total_received:>10}   "
                    f"frames {s['frames_ok']:>6}   "
                    f"bad CRC {s['frames_bad_crc']:>4}   "
                    f"missing frames {s['frames_missing']:>4}   "
                    f"missing samples {s['samples_missing']:>6}"
                )

                last_report = now

            time.sleep(0.05)

    except KeyboardInterrupt:

        print("\nStopping...")

    finally:

        stop.set()

        t.join(timeout=3.0)

        if t.is_alive():
            errors.append("Reader thread did not stop cleanly.")

        # Flush remaining samples.
        with sink_lock:

            if sink:

                out_file.write(sink)

                samples_written += len(sink) // 2

                sink.clear()

        out_file.close()
        ser.close()

    # ---------------------------------------------------------------------
    # Final report
    # ---------------------------------------------------------------------

    elapsed = time.monotonic() - start

    s = parser.stats

    print("\nRecording complete.")

    print(
        f"Duration: {elapsed:.2f} seconds"
    )

    print(
        f"Samples received: {total_received:,}"
    )

    print(
        f"Samples written: {samples_written:,}"
    )

    print(
        f"Measured sample rate: "
        f"{total_received / elapsed if elapsed > 0 else 0:.1f} Hz"
    )

    print(
        f"Expected sample rate: {args.expected_rate:.1f} Hz"
    )

    print("\nFrame statistics:")

    print(
        f"  Valid frames:       {s['frames_ok']:,}"
    )

    print(
        f"  Bad CRC:            {s['frames_bad_crc']:,}"
    )

    print(
        f"  Bad headers:        {s['bad_headers']:,}"
    )

    print(
        f"  Missing frames:     {s['frames_missing']:,}"
    )

    print(
        f"  Reported drops:     {s['reported_drops']:,}"
    )

    print(
        f"  Missing samples:    {s['samples_missing']:,}"
    )

    print(
        f"  Sample gaps:        {s['sample_gaps']:,}"
    )

    print(
        f"  Index resets:       {s['sample_index_resets']:,}"
    )

    print(
        f"  Counter mismatches: {s['dropped_counter_mismatches']:,}"
    )

    print(
        f"  Resynchronizations: {s['resyncs']:,}"
    )

    print(
        f"  Bytes discarded:    {s['bytes_discarded']:,}"
    )

    print(
        f"\nSaved {samples_written * 2:,} bytes to {args.out}"
    )

    if errors:
        print(f"\nWarning: {errors[-1]}")

    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())
