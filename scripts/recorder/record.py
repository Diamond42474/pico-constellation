#!/usr/bin/env python3
"""
Record raw ADC samples streamed from a Pico Constellation recorder.

Usage:
    python record.py --port COM5
    python record.py --port /dev/ttyACM0 --duration 30 --out capture.raw
    python record.py --port COM5 --expected-rate 44100

The wire protocol, serial source, and recording session live in the
recorder_core package; this file is the command-line front end.

Samples are saved as raw uint16 little-endian data, compatible with:

    np.fromfile(FILENAME, dtype=np.uint16)
"""

import argparse
import sys
import threading
import time

from recorder_core.session import RecordingSession
from recorder_core.source import SerialSource

SINK_FLUSH_SIZE = 65536


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

    source = SerialSource(args.port)
    stop = threading.Event()

    # Reader callback appends validated little-endian sample bytes here.
    sink = bytearray()
    sink_lock = threading.Lock()

    # Total validated samples received from the Pico.
    total_received = 0

    def on_frame(frame):
        nonlocal total_received
        _seq, _first_sample_index, _dropped_since_last, samples = frame
        with sink_lock:
            sink.extend(samples)
        total_received += len(samples) // 2

    source.subscribe(on_frame)

    try:
        source.connect()

    except KeyboardInterrupt:
        print("\nAborted before connecting.")
        return 1

    session = RecordingSession(args.out)

    source.start()

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

        while not stop.is_set() and not source.finished:

            now = time.monotonic()

            if (
                args.duration is not None
                and now - start >= args.duration
            ):
                break

            # Flush buffered sample data to disk.
            with sink_lock:

                if len(sink) >= SINK_FLUSH_SIZE:

                    session.write(sink)

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

                s = source.stats

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

        source.stop()

        # Flush remaining samples.
        with sink_lock:

            if sink:

                session.write(sink)

                sink.clear()

        session.close()

    # ---------------------------------------------------------------------
    # Final report
    # ---------------------------------------------------------------------

    elapsed = time.monotonic() - start

    s = source.stats

    samples_written = session.samples_written

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

    if source.errors:
        print(f"\nWarning: {source.errors[-1]}")

    return 0 if not source.errors else 1


if __name__ == "__main__":
    sys.exit(main())
