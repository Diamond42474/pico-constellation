#!/usr/bin/env python3
"""
Record raw ADC samples streamed from a Pico Constellation recorder.

The firmware (pico-constellation/src/recorder.c) streams little-endian
frames over USB CDC:

    magic   uint16  0x5AA5
    seq     uint16  frame sequence number (wraps at 16 bits)
    count   uint16  number of samples in this frame
    samples uint16[count]   raw 12-bit ADC samples
    crc     uint16  CRC-16/CCITT-FALSE over all preceding bytes

The parser resynchronizes on the magic word after any byte loss, drops
frames that fail the CRC, and reports gaps in the sequence number.

Samples are written to a .raw file as raw uint16 (little-endian) with no
header, matching the convention used by scripts/dsp.py
(np.fromfile(FILENAME, dtype=np.uint16)).

Usage:
    python record.py --port COM5
    python record.py --port /dev/ttyACM0 --duration 30 --out capture.raw
    python record.py --port COM5 --expected-rate 44100
"""

import argparse
import array
import struct
import sys
import threading
import time

MAGIC = 0x5AA5
HEADER_SIZE = 6  # magic + seq + count
FOOTER_SIZE = 2  # crc
MAX_FRAME_SAMPLES = 4096  # Sanity bound for count; larger means desync


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). Must match the firmware."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


class FrameParser:
    """
    Incremental frame parser. Feed it raw bytes (any chunk size); it
    yields (seq, samples) tuples for every good frame.

    stats: dict with keys
        frames_ok      frames with a valid CRC
        frames_bad_crc frames whose CRC failed
        resyncs        times we had to hunt for the magic word
        bytes_discarded bytes thrown away while resynchronizing
        seq_gaps       number of missing frames (sequence number gaps)
    """

    def __init__(self):
        self._buf = bytearray()
        self._expect_seq = None
        self.stats = {
            "frames_ok": 0,
            "frames_bad_crc": 0,
            "resyncs": 0,
            "bytes_discarded": 0,
            "seq_gaps": 0,
        }

    def feed(self, data: bytes):
        """Feed raw bytes; returns a list of (seq, samples) good frames."""
        self._buf.extend(data)
        frames = []
        buf = self._buf
        magic = struct.pack("<H", MAGIC)

        while True:
            idx = buf.find(magic)
            if idx < 0:
                # Keep the last byte in case it is the first magic byte
                del buf[: max(0, len(buf) - 1)]
                if len(buf) > 2048:
                    # No magic anywhere in a big pile of data: discard all but last byte
                    del buf[:-1]
                    self.stats["bytes_discarded"] += len(data)
                break

            if idx > 0:
                self.stats["resyncs"] += 1
                self.stats["bytes_discarded"] += idx
                del buf[:idx]

            if len(buf) < HEADER_SIZE:
                break  # Wait for the rest of the header

            seq, count = struct.unpack_from("<HH", buf, 2)
            if count == 0 or count > MAX_FRAME_SAMPLES:
                # Header is garbage: step one byte past this magic and rescan
                del buf[0]
                self.stats["resyncs"] += 1
                self.stats["bytes_discarded"] += 1
                continue

            frame_len = HEADER_SIZE + count * 2 + FOOTER_SIZE
            if len(buf) < frame_len:
                break  # Wait for the rest of the frame

            frame = bytes(buf[:frame_len])
            del buf[:frame_len]

            payload, crc_rx = frame[:-FOOTER_SIZE], struct.unpack_from("<H", frame, -FOOTER_SIZE)[0]
            if crc16_ccitt(payload) != crc_rx:
                self.stats["frames_bad_crc"] += 1
                continue  # Corrupted: rescan from the next byte (may resync mid-frame)

            self.stats["frames_ok"] += 1
            if self._expect_seq is not None:
                expected = self._expect_seq
                if seq != expected:
                    self.stats["seq_gaps"] += (seq - expected) & 0xFFFF
            self._expect_seq = (seq + 1) & 0xFFFF

            samples = array.array("H")
            samples.frombytes(payload[HEADER_SIZE:])
            frames.append((seq, samples))

        return frames


def open_serial(port: str):
    """Open the serial port, waiting for the device to appear."""
    import serial  # Lazy import so the parser is testable without pyserial

    print(f"Opening {port} ...")
    while True:
        try:
            ser = serial.Serial(port, 115200, timeout=1.0)
            print(f"Connected to {port}")
            return ser
        except (serial.SerialException, OSError) as e:
            print(f"Could not open {port}: {e}")
            print("Is the Pico plugged in? (retrying every 2 s, Ctrl+C to quit)")
            time.sleep(2.0)


def main():
    ap = argparse.ArgumentParser(description="Record ADC samples from a Pico Constellation recorder.")
    ap.add_argument("--port", required=True, help="Serial port of the Pico (e.g. COM5 or /dev/ttyACM0)")
    ap.add_argument("--out", default="capture.raw", help="Output .raw file (uint16 little-endian, no header). Default: capture.raw")
    ap.add_argument("--duration", type=float, default=None, help="Record for N seconds, then stop (default: until Ctrl+C)")
    ap.add_argument("--expected-rate", type=float, default=44100.0, help="Nominal sample rate in Hz, for reference (default: 44100)")
    args = ap.parse_args()

    try:
        ser = open_serial(args.port)
    except KeyboardInterrupt:
        print("\nAborted before connecting.")
        return 1

    parser = FrameParser()
    stop = threading.Event()
    errors = []

    # Sample sink shared with the reader thread
    sink = array.array("H")
    sink_lock = threading.Lock()
    samples_written = [0]
    total_samples = [0]

    out_file = open(args.out, "wb")

    def reader():
        empty_streak = 0
        try:
            while not stop.is_set():
                data = ser.read(4096)
                if not data:
                    # pyserial returns b'' both on timeout and on a dead
                    # device; only treat a sustained gap as a disconnect.
                    empty_streak += 1
                    if empty_streak >= 5:  # ~5 s with no data at all
                        errors.append("No data for ~5 s (device disconnected?)")
                        break
                    continue
                empty_streak = 0
                for _seq, samples in parser.feed(data):
                    with sink_lock:
                        sink.extend(samples)
                    total_samples[0] += len(samples)
        except Exception as e:
            errors.append(str(e))
            stop.set()

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    start = time.monotonic()

    print(f"Recording to {args.out} (expected ~{args.expected_rate:.0f} Hz). Ctrl+C to stop.")
    try:
        last_report = time.monotonic()
        while not stop.is_set():
            if args.duration is not None and (time.monotonic() - start) >= args.duration:
                break

            # Flush buffered samples to disk
            with sink_lock:
                if len(sink) >= 65536:
                    out_file.write(sink.tobytes())
                    samples_written[0] += len(sink)
                    del sink[:]

            # Report stats once per second
            now = time.monotonic()
            if now - last_report >= 1.0:
                elapsed = now - start
                rate = (total_samples[0] + samples_written[0]) / elapsed if elapsed > 0 else 0.0
                s = parser.stats
                print(
                    f"  {rate:8.0f} Hz   "
                    f"samples {total_samples[0] + samples_written[0]:>10}   "
                    f"frames ok {s['frames_ok']:>6}  bad {s['frames_bad_crc']:>4}  "
                    f"gaps {s['seq_gaps']:>4}"
                )
                last_report = now

            time.sleep(0.05)
    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        stop.set()
        t.join(timeout=3.0)
        with sink_lock:
            if sink:
                out_file.write(sink.tobytes())
                samples_written[0] += len(sink)
        out_file.close()
        ser.close()

    total = total_samples[0] + samples_written[0]
    elapsed = time.monotonic() - start
    s = parser.stats
    print(
        f"\nDone. {total} samples in {elapsed:.1f} s "
        f"({total / elapsed:.0f} Hz measured, {args.expected_rate:.0f} Hz expected)."
    )
    print(f"Frames: {s['frames_ok']} ok, {s['frames_bad_crc']} bad CRC, {s['seq_gaps']} missing (seq gaps).")
    if s["bytes_discarded"]:
        print(f"Resynced {s['resyncs']} time(s), discarded {s['bytes_discarded']} stray byte(s).")
    if errors:
        print(f"Note: {errors[-1]}")
    print(f"Saved {total * 2} bytes to {args.out}")

    return 0 if not errors else 1


if __name__ == "__main__":
    sys.exit(main())