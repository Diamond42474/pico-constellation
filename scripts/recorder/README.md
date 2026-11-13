# Pico Constellation — Recorder Host Tool

Host-side software for the Pico Constellation ADC recorder firmware
(`pico-constellation/src/recorder.c`). It captures the raw ADC sample
stream from the Pico over USB serial and saves it to a file, with
frame-level integrity checking and live statistics.

## What it does

- Opens the Pico's USB serial port (CDC-ACM, 115200 baud) and reads the
  recorder stream: 512-sample frames, each with a 14-byte header and a
  2-byte CRC-16 footer.
- Validates every frame (sync word, header fields, CRC-16/CCITT) and
  tracks stream health: missing frames, missing samples, firmware-reported
  drops, index resets, and resynchronizations.
- Writes validated samples as raw little-endian uint16 data at 44.1 kHz —
  ready for plotting, DSP, or conversion to WAV.
- Prints a live status line (measured rate, sample/frame counts, error
  counts) and a final statistics report.

## Requirements

- Python 3.8+
- [pyserial](https://pypi.org/project/pyserial/) — `pip install -r requirements.txt`

## Usage

From this directory:

```bash
python record.py --port COM5 --duration 30 --out capture.raw
```

On Linux/macOS the port is usually `/dev/ttyACM0`:

```bash
python record.py --port /dev/ttyACM0 --duration 30 --out capture.raw
```

If the device is not plugged in yet, the tool waits and retries every
2 seconds. Stop early with `Ctrl+C`; buffered samples are still flushed
to the output file.

| Option | Default | Description |
|---|---|---|
| `--port` | (required) | Serial port, e.g. `COM5` or `/dev/ttyACM0` |
| `--out` | `capture.raw` | Output file for raw sample data |
| `--duration` | run until Ctrl+C | Recording length in seconds |
| `--expected-rate` | `44100` | Nominal ADC rate, used in the status line |

### Launcher

`record.sh` (Linux/macOS, also works in git-bash on Windows) or
`record.bat` (Windows) creates a local
virtual environment, installs the requirements, lists available serial
ports, and starts the recorder. If Python 3 is not installed, the
launcher prints an install hint and exits. Arguments are forwarded to
`record.py`:

```bash
./record.sh --port /dev/ttyACM0 --duration 30     # Linux/macOS
record.bat --port COM5 --duration 30              # Windows
```

### GUI

`record_gui.py` is a tkinter desktop app that shows the ADC stream live
and records it on demand:

```bash
python record_gui.py        # or ./gui.sh / gui.bat
```

The stream starts as soon as the selected port connects (pick from the
dropdown, or press Refresh; it auto-reconnects if the device is
re-plugged) and the waveform updates continuously — last 5 seconds,
min/max decimated. **Start Recording** / **Stop Recording** only
controls whether samples are written to disk; stopping does not stop
the stream. Captures are saved as timestamped files in the git-ignored
`recordings/` folder:

```
recordings/capture_2026-11-13_15-07-32.raw
```

The status bar shows the live state (Streaming/Recording), rate, sample
count, frames, missing samples, firmware-reported drops, and CRC
errors. If the device is unplugged mid-recording, the capture is saved
automatically.

## Output format

Raw little-endian unsigned 16-bit samples, no header, at 44.1 kHz.
Load and convert to WAV with:

```python
import numpy as np
from scipy.io import wavfile

data = np.fromfile("capture.raw", dtype="<u2").astype(np.float32)
wavfile.write("capture.wav", 44100, data)
```

## Wire protocol

Little-endian; each frame carries 512 samples.

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Sync word `0x5AA5` |
| 2 | 2 | Frame sequence number (wraps at 65536) |
| 4 | 4 | Sample index of the first sample in the frame |
| 8 | 4 | Sample count (always 512) |
| 12 | 2 | Samples dropped by the firmware since the previous frame |
| 14 | 1024 | Sample payload (512 × uint16) |
| 1038 | 2 | CRC-16/CCITT over bytes 0–1037 |

The sample index and dropped-sample counter let the host detect and
quantify any loss (USB stall, ring overflow on the device), rather than
silently producing a gap.

## Files

- `record.py` — recorder, frame parser, CLI
- `record_gui.py` — tkinter GUI: live waveform + on-demand recording
- `recorder_core/` — shared library (protocol, serial source, session, plotting)
- `record.sh` / `record.bat` — CLI venv launchers (Linux / macOS / Windows)
- `gui.sh` / `gui.bat` — GUI venv launchers (Linux / macOS / Windows)
- `test_recorder_protocol.py` — offline protocol test (no hardware needed)
- `test_recording_session.py` — offline recording-file test (no hardware needed)
- `requirements.txt`

## Testing

The parser and protocol are covered by an offline test that transcribes
the firmware's ring buffer and exercises the real `FrameParser` with
clean streams, frame loss, USB stalls, and byte corruption:

```bash
python test_recorder_protocol.py
```

Exits 0 on success. Requires only the Python standard library.
