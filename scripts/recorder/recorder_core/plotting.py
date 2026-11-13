"""Real-time display helpers: rolling sample buffer and decimation."""

import numpy as np


class RollingBuffer:
    """Fixed-capacity buffer that keeps the most recent samples.

    When full, the oldest samples are dropped to make room for new ones.
    """

    def __init__(self, capacity):
        self.capacity = int(capacity)
        self._buf = np.empty(self.capacity, dtype=np.uint16)
        self._len = 0

    def append(self, samples):
        """Append samples (array-like or raw bytes of uint16 values)."""

        s = np.frombuffer(samples, dtype="<u2") if isinstance(
            samples, (bytes, bytearray)) else np.asarray(samples)

        n = len(s)

        if n == 0:
            return

        if n >= self.capacity:
            self._buf[:] = s[-self.capacity:]
            self._len = self.capacity
            return

        if self._len + n > self.capacity:
            keep = self.capacity - n
            self._buf[:keep] = self._buf[self._len - keep:self._len]
            self._len = keep

        self._buf[self._len:self._len + n] = s
        self._len += n

    def window(self, n):
        """Return the last n samples (or fewer if not enough yet)."""

        n = min(n, self._len)
        return self._buf[self._len - n:self._len]

    def __len__(self):
        return self._len


def decimate_minmax(samples, target_points):
    """Reduce samples to ~target_points using per-bucket min/max.

    Returns (x, ymin, ymax) arrays with x in sample-index units. Min/max
    decimation preserves peaks and zero-crossings that plain subsampling
    would miss, so the waveform envelope stays honest.
    """

    samples = np.asarray(samples)
    n = len(samples)

    if n <= target_points:
        x = np.arange(n, dtype=np.float64)
        return x, samples.astype(np.float64), samples.astype(np.float64)

    bucket = int(np.ceil(n / target_points))
    count = n // bucket

    trimmed = samples[:count * bucket]

    shaped = trimmed.reshape(count, bucket)

    ymin = shaped.min(axis=1).astype(np.float64)
    ymax = shaped.max(axis=1).astype(np.float64)
    x = (np.arange(count) * bucket + bucket // 2).astype(np.float64)

    # Keep any remainder samples visible.
    remainder = n - count * bucket
    if remainder > 0:
        tail = samples[count * bucket:]
        x = np.append(x, count * bucket + remainder // 2)
        ymin = np.append(ymin, tail.min())
        ymax = np.append(ymax, tail.max())

    return x, ymin, ymax
