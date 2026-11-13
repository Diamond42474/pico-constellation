#!/usr/bin/env python3
"""
Tests for recorder_core.session: timestamped recording files.

Runs without hardware; uses a temporary directory.
"""
import os
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from recorder_core.session import RECORDINGS_DIR, RecordingSession


def main():
    ok = True

    with tempfile.TemporaryDirectory() as tmp:

        # 1. open() creates the directory and a timestamped .raw file
        t = time.struct_time((2026, 11, 13, 15, 7, 32, 0, 0, -1))
        s = RecordingSession.open(directory=tmp, now=t)
        expected = os.path.join(tmp, "capture_2026-11-13_15-07-32.raw")
        if str(s.path) != expected:
            print(f"FAIL: path {s.path} != {expected}")
            ok = False
        if not os.path.exists(s.path):
            print("FAIL: file not created")
            ok = False
        s.close()

        # 2. same-second collision gets a _2 suffix
        s2 = RecordingSession.open(directory=tmp, now=t)
        expected2 = os.path.join(tmp, "capture_2026-11-13_15-07-32_2.raw")
        if str(s2.path) != expected2:
            print(f"FAIL: collision path {s2.path} != {expected2}")
            ok = False
        s2.close()

        # 3. custom prefix
        s3 = RecordingSession.open(directory=tmp, prefix="test", now=t)
        if os.path.basename(str(s3.path)) != "test_2026-11-13_15-07-32.raw":
            print(f"FAIL: prefix not honored: {s3.path}")
            ok = False
        s3.close()

        # 4. write() counts samples and writes bytes
        s4 = RecordingSession.open(directory=tmp, now=t)
        s4.write(b"\x01\x00" * 100)          # 100 samples, 200 bytes
        s4.write(b"\x02\x00" * 50)           # 50 more
        if s4.samples_written != 150:
            print(f"FAIL: samples_written {s4.samples_written} != 150")
            ok = False
        s4.close()
        if os.path.getsize(s4.path) != 300:
            print(f"FAIL: file size {os.path.getsize(s4.path)} != 300")
            ok = False

        # 5. summary mentions sample count
        if "150 samples" not in s4.summary():
            print(f"FAIL: summary missing count: {s4.summary()}")
            ok = False

        # 6. explicit path via constructor
        p = os.path.join(tmp, "explicit.raw")
        s5 = RecordingSession(p)
        s5.write(b"\x00\x00")
        s5.close()
        if not os.path.exists(p) or os.path.getsize(p) != 2:
            print("FAIL: explicit path write")
            ok = False

    # 7. default recordings dir location
    parts = RECORDINGS_DIR.parts[-3:]
    if parts != ("scripts", "recorder", "recordings"):
        print(f"FAIL: RECORDINGS_DIR parts {parts}")
        ok = False

    print()
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
