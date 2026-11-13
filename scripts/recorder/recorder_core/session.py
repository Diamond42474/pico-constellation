"""Recording session: write validated samples to a timestamped file.

Default output location is the git-ignored ``recordings/`` folder next
to the recorder tool. Files are named with a local-time timestamp:

    recordings/capture_2026-11-13_15-07-32.raw
"""

import time
from pathlib import Path

# scripts/recorder/recordings/ (recorder_core/ -> recorder/)
RECORDINGS_DIR = Path(__file__).resolve().parent.parent / "recordings"

FILENAME_PREFIX = "capture"

SAMPLE_RATE = 44100


class RecordingSession:
    """An open recording file.

    Use ``RecordingSession.open()`` for the default timestamped file in
    the recordings folder, or construct directly with an explicit path.
    """

    def __init__(self, path):
        self.path = Path(path)
        self._file = open(self.path, "wb")
        self.samples_written = 0

    @classmethod
    def open(cls, directory=None, prefix=FILENAME_PREFIX, now=None):
        """Open a new recording file.

        directory: output folder (default: the git-ignored recordings dir)
        prefix: filename prefix (default: "capture")
        now: optional time value for testing (default: current local time)
        """

        directory = Path(directory) if directory else RECORDINGS_DIR

        directory.mkdir(parents=True, exist_ok=True)

        stamp = time.strftime(
            "%Y-%m-%d_%H-%M-%S",
            now if now is not None else time.localtime(),
        )

        path = directory / f"{prefix}_{stamp}.raw"

        suffix = 2
        while path.exists():
            path = directory / f"{prefix}_{stamp}_{suffix}.raw"
            suffix += 1

        return cls(path)

    def write(self, sample_bytes):
        """Append raw little-endian uint16 sample bytes."""

        self._file.write(sample_bytes)
        self.samples_written += len(sample_bytes) // 2

    def close(self):
        if self._file is not None:
            self._file.close()
            self._file = None

    def summary(self):
        seconds = self.samples_written / SAMPLE_RATE
        return (
            f"{self.samples_written:,} samples "
            f"({seconds:.2f} s) -> {self.path}"
        )

    def __enter__(self):
        return self

    def __exit__(self, *exc_info):
        self.close()
