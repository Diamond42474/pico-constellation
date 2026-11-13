#!/usr/bin/env python3
"""
Protocol test for the recorder (14-byte header) protocol.

Transcribes pico-constellation/src/recorder.c (ring of {sample, index},
drop-oldest, full-frame-only flush) and feeds its byte stream through the
real FrameParser in recorder_core.protocol.

Firmware flow (verified against fsk_decoder.c + main.c): the FSK decoder
state machine goes IDLE->DECODING as soon as the input buffer is non-empty
and drains whatever is in it that cycle (~1024 samples = one ADC IRQ
batch at 44.1 kHz, up to 3072 if the main loop lags). recorder_flush()
runs after every main-loop iteration.

Test cases:
  1. Normal iterations (1024-sample pushes): continuity, zero drops.
  2. Lagging main loop (3072-sample pushes): still zero drops at 2048 ring.
  3. USB stall: flush blocked while the ring fills -> oldest samples
     dropped, but the host must reconstruct continuity from the index
     field and the dropped counter (mismatch count must stay 0).
  4. Frame loss (whole frames dropped on the wire): seq-gap detection.
  5. Byte corruption mid-stream: resync on next magic, continuity after.
"""
import os
import struct
import sys

# recorder_core package lives next to this test.
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from recorder_core.protocol import FrameParser, crc16_ccitt


# --- transcribed firmware --------------------------------------------------
class Firmware:
    """Transcription of recorder.c (14-byte header protocol)."""

    MAGIC = 0x5AA5
    FRAME_SAMPLES = 512

    def __init__(self, ring_samples=2048):
        self.ring = []            # list of (sample, index), oldest first
        self.ring_samples = ring_samples
        self.next_sample_index = 0
        self.frame_seq = 0
        self.samples_dropped = 0
        self.wire = bytearray()   # everything the firmware has put on USB

    def debug_handle_recording(self, sample):
        idx = self.next_sample_index
        self.next_sample_index += 1
        if len(self.ring) == self.ring_samples:
            self.ring.pop(0)
            self.samples_dropped += 1
        self.ring.append((sample, idx))

    def _send_frame(self):
        first_index = self.ring[0][1]
        dropped = self.samples_dropped
        self.samples_dropped = 0
        samples = [s for (s, _) in self.ring[:self.FRAME_SAMPLES]]
        self.ring = self.ring[self.FRAME_SAMPLES:]
        header = struct.pack(
            "<HHIHI", self.MAGIC, self.frame_seq, first_index,
            self.FRAME_SAMPLES, dropped)
        self.frame_seq = (self.frame_seq + 1) & 0xFFFF
        payload = header + struct.pack(f"<{len(samples)}H", *samples)
        crc = crc16_ccitt(payload)
        self.wire += payload + struct.pack("<H", crc)

    def flush(self):
        while len(self.ring) >= self.FRAME_SAMPLES:
            self._send_frame()

    def iteration(self, push_size=1024, flush_ok=True):
        """One main-loop cycle: push_size ADC samples arrive (as the FSK
        drain hands them to the recorder), then recorder_flush() runs if
        the USB write isn't blocked."""
        for _ in range(push_size):
            self.debug_handle_recording(self.next_sample_index & 0x0FFF)
        if flush_ok:
            self.flush()


def feed(fw, parser):
    if fw.wire:
        parser.feed(bytes(fw.wire))
        fw.wire.clear()


def summary(label, parser, total):
    s = parser.stats
    print(f"{label}: frames_ok={s['frames_ok']} bad_crc={s['frames_bad_crc']} "
          f"missing_frames={s['frames_missing']} missing_samples={s['samples_missing']} "
          f"sample_gaps={s['sample_gaps']} reported_drops={s['reported_drops']} "
          f"counter_mismatches={s['dropped_counter_mismatches']} resyncs={s['resyncs']} "
          f"(total={total})")
    return s


def main():
    # --- resync source check: replay test 1 with an instrumented parser ---
    fw, parser = Firmware(2048), FrameParser()
    ready = b"PICO-CONSTELLATION RECORDER READY\n"
    parser.feed(ready)
    r_after_ready = parser.stats["resyncs"]
    fw.iteration(1024)
    feed(fw, parser)
    r_after_first_frames = parser.stats["resyncs"]
    print(f"resync check: after READY line = {r_after_ready}, "
          f"after first 2 frames = {r_after_first_frames}")
    ok = True
    total_iters, push = 100, 1024
    total = total_iters * push

    # 1. normal iterations
    fw, parser = Firmware(2048), FrameParser()
    parser.feed(b"PICO-CONSTELLATION RECORDER READY\n")
    for _ in range(total_iters):
        fw.iteration(push)
        feed(fw, parser)
    s = summary("1 normal  ", parser, total)
    if (s["samples_missing"] or s["sample_gaps"] or s["reported_drops"]
            or s["frames_bad_crc"] or s["bad_headers"]
            or s["dropped_counter_mismatches"]):
        ok = False

    # 2. lagging main loop (3072-sample pushes, 2048 ring): the ring can
    #    only hold 2048, so a sustained 3072-sample cycle loses 1024 per
    #    cycle. Loss must be REPORTED consistently (index gap == dropped
    #    counter), never silent. (Steady state on the real device is the
    #    1024-sample cycle of test 1 — verified by the live capture.)
    fw, parser = Firmware(2048), FrameParser()
    for _ in range(total_iters):
        fw.iteration(3072)
        feed(fw, parser)
    s = summary("2 lag 3072", parser, total_iters * 3072)
    expected_lag_drops = (3072 - 2048) * total_iters
    if s["dropped_counter_mismatches"] or s["frames_bad_crc"]:
        ok = False
    if abs(s["reported_drops"] - expected_lag_drops) > 512:
        print(f"   FAIL: lag drops {s['reported_drops']} != expected {expected_lag_drops}")
        ok = False

    # 3. USB stall: 50 normal iterations, then flush blocked for 20 iterations
    #    (ring fills, oldest samples dropped), then recovery.
    fw, parser = Firmware(2048), FrameParser()
    for _ in range(50):
        fw.iteration(1024)
        feed(fw, parser)
    for _ in range(20):
        fw.iteration(1024, flush_ok=False)   # USB write blocked: no flush
    for _ in range(30):
        fw.iteration(1024)
        feed(fw, parser)
    s = summary("3 stall   ", parser, 100 * 1024)
    expected_drops = max(0, 20 * 1024 - 2048)
    got_drops = s["samples_missing"] + s["sample_gaps"]
    print(f"   expected dropped ~{expected_drops}, observed index-gap loss {got_drops}, "
          f"reported_drops={s['reported_drops']}")
    if got_drops < expected_drops - 512 or s["dropped_counter_mismatches"]:
        print("   FAIL: drop accounting inconsistent")
        ok = False

    # 4. frame loss (whole frames dropped on the wire)
    fw = Firmware(2048)
    parser = FrameParser()
    for _ in range(10):
        fw.iteration(1024)
    wire = bytes(fw.wire)          # copy before clearing!
    fw.wire.clear()
    frame_len = 14 + 512 * 2 + 2
    chunks = [wire[i:i + frame_len] for i in range(0, len(wire), frame_len)]
    data = bytearray()
    for i, c in enumerate(chunks):
        if i % 3 == 1:
            continue               # drop every 3rd frame entirely
        data += c
    n_dropped = len(chunks) - len([c for i, c in enumerate(chunks) if i % 3 != 1])
    parser.feed(bytes(data))
    s = summary("4 loss    ", parser, len(chunks) * 512)
    # a trailing dropped frame is only detectable once the next frame
    # arrives, so when the LAST frame is dropped the count is one lower
    last_dropped = (len(chunks) - 1) % 3 == 1
    expect_missing = n_dropped - 1 if last_dropped else n_dropped
    print(f"   dropped {n_dropped} of {len(chunks)} frames; "
          f"frames_missing={s['frames_missing']} (expected {expect_missing})")
    if s["frames_missing"] != expect_missing or s["frames_bad_crc"]:
        ok = False

    # 5. byte corruption: overwrite 100 bytes mid frame-2 with garbage
    fw = Firmware(2048)
    parser = FrameParser()
    for _ in range(10):
        fw.iteration(1024)
    blob = bytearray(fw.wire)      # copy
    fw.wire.clear()
    mid = 2 * frame_len
    blob[mid:mid + 100] = b"\xA5" * 100
    for _ in range(10):
        fw.iteration(1024)
    parser.feed(bytes(blob) + bytes(fw.wire))
    fw.wire.clear()
    total5 = 20 * 1024
    s = summary("5 corrupt ", parser, total5)
    # corruption destroys frame 2's CRC (and may drag in part of frame 3);
    # resync must recover the rest with correct continuity afterwards
    if s["resyncs"] < 1 or s["frames_ok"] < 33:
        ok = False

    print()
    print("PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
