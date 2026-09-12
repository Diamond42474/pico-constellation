"""Convert recorded Pico CSV samples into raw ADC and centered WAV files."""

import argparse
import csv
import sys
import wave
from array import array
from pathlib import Path


DEFAULT_SAMPLE_RATE = 48_000
ADC_CENTER = 2_048


def parse_arguments():
    """Return command-line options for a single recording conversion."""

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "input_csv",
        type=Path,
        help="CSV file produced by scripts/record.py",
    )
    parser.add_argument(
        "--sample-rate",
        type=int,
        default=DEFAULT_SAMPLE_RATE,
        help=f"WAV sample rate in Hz (default: {DEFAULT_SAMPLE_RATE})",
    )
    parser.add_argument(
        "--raw-output",
        type=Path,
        help="Raw uint16 output path (default: capture.raw beside the CSV)",
    )
    parser.add_argument(
        "--wav-output",
        type=Path,
        help="Centered WAV output path (default: capture_centered.wav beside the CSV)",
    )
    return parser.parse_args()


def read_samples(input_csv):
    """Read and validate unsigned 16-bit ADC samples from a recorder CSV."""

    try:
        with input_csv.open(newline="", encoding="utf-8") as csv_file:
            reader = csv.DictReader(csv_file)

            if not reader.fieldnames or "sample" not in reader.fieldnames:
                raise ValueError("CSV must contain a 'sample' column.")

            samples = []
            for line_number, row in enumerate(reader, start=2):
                try:
                    sample = int(row["sample"])
                except (TypeError, ValueError) as error:
                    raise ValueError(
                        f"Invalid sample on CSV line {line_number}: {row['sample']!r}"
                    ) from error

                if not 0 <= sample <= 0xFFFF:
                    raise ValueError(
                        f"Sample on CSV line {line_number} is outside uint16 range: "
                        f"{sample}"
                    )

                samples.append(sample)
    except OSError as error:
        raise ValueError(f"Could not read {input_csv}: {error}") from error

    if not samples:
        raise ValueError("CSV contains no samples.")

    return samples


def as_little_endian_bytes(values, typecode):
    """Serialize an array using the little-endian PCM byte order."""

    output = array(typecode, values)
    if sys.byteorder != "little":
        output.byteswap()
    return output.tobytes()


def write_raw(output_path, samples):
    """Write unmodified CSV samples as little-endian uint16 values."""

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(as_little_endian_bytes(samples, "H"))


def write_centered_wav(output_path, samples, sample_rate):
    """Write samples, shifted around zero, as a 16-bit mono PCM WAV."""

    centered_samples = [sample - ADC_CENTER for sample in samples]
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with wave.open(str(output_path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(as_little_endian_bytes(centered_samples, "h"))


def main():
    """Convert one recording CSV to capture.raw and a centered WAV file."""

    arguments = parse_arguments()
    if arguments.sample_rate <= 0:
        raise ValueError("Sample rate must be greater than zero.")

    input_csv = arguments.input_csv
    raw_output = arguments.raw_output or input_csv.with_name("capture.raw")
    wav_output = arguments.wav_output or input_csv.with_name("capture_centered.wav")

    samples = read_samples(input_csv)
    write_raw(raw_output, samples)
    write_centered_wav(wav_output, samples, arguments.sample_rate)

    print(f"Converted {len(samples)} samples from {input_csv}")
    print(f"Raw samples: {raw_output}")
    print(f"Centered WAV ({arguments.sample_rate} Hz): {wav_output}")


if __name__ == "__main__":
    main()
