# Pico Constellation

A digital -> analog / analog -> digital library intended for encoding and decoding data over analog audio systems. i.e. analog radios.

## Recording Pico debug data

`scripts/record.py` displays the raw ADC-sample stream from a Raspberry Pi Pico
2 W and can save a fixed number of incoming samples to CSV. The firmware sends
1,024-sample frames with a 32-bit marker, sequence number, sample count, and
CRC32. `record.py` validates every frame and reports sequence gaps or corrupt
frames before recording their samples. Build and flash the matching firmware
before starting the Python recorder.

### Prepare the firmware

1. In [`lib/peregrine-constellation/Core/Inc/interface/pconfig.h`](lib/peregrine-constellation/Core/Inc/interface/pconfig.h), enable debug recording:

   ```c
   #define pconfig_DEBUG_RECORDING_ENABLED (1)
   ```

2. Build the Pico application with the recorder entry point. In
   [`pico-constellation/CMakeLists.txt`](pico-constellation/CMakeLists.txt), the
   `src/recorder.c` option is the active source-file selection. Use this option
   when building the firmware intended to emit recording data, then flash the
   resulting image to the Pico.

   The recorder writes binary records to USB serial in batches, so the first
   records may not appear until the firmware has filled its buffer.

### Run the desktop recorder

Install the Python dependencies once:

```sh
python3 -m pip install -r scripts/requirements.txt
```

With the Pico connected as `/dev/ttyACM0`, run:

```sh
python3 scripts/record.py
```

If the device uses another serial path, change `DEVICE` near the top of
`scripts/record.py`. The application waits for the device if it is not yet
connected and reconnects after a disconnect.

Enter the desired number of samples in **Samples to record**, then click
**Start recording**. Every incoming record is captured, even though the graph
is decimated for display. The recorder stops automatically at the requested
count; **Stop recording** preserves any samples captured so far.

CSV files are written to `scripts/recorded_data/` with a timestamped filename.
They contain `timestamp_unix` and `sample` columns.

To create analysis files from a recording, pass its CSV path to
`center-shift.py`:

```sh
python3 scripts/center-shift.py scripts/recorded_data/recording_YYYYMMDD_HHMMSS.csv
```

It writes the unmodified 16-bit ADC samples to `capture.raw` and centered
16-bit mono PCM audio to `capture_centered.wav`, both beside the input CSV. The
WAV sample rate defaults to 48,000 Hz and can be overridden with
`--sample-rate`.
