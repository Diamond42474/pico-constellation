import numpy as np
import matplotlib.pyplot as plt
from scipy.signal import butter, lfilter

# ===== CONFIG =====
FILENAME = "capture.raw"
SAMPLE_RATE = 79200  # change if different
LOW_TONE = 1200
HIGH_TONE = 2200
BAUD_RATE = 32

# ===== LOAD RAW DATA =====
data = np.fromfile(FILENAME, dtype=np.uint16)

# Convert uint16 → centered float (-1 to 1)
data = data.astype(np.float32)
data = (data - 2048) / 2048.0

data = data * 2

sos1200 = butter(4, [1000, 1400], btype='band', fs=70400, output='sos')
sos2200 = butter(4, [2000, 2400], btype='band', fs=70400, output='sos')

#print("1200 SOS:")
#print(sos1200)

#print("2200 SOS:")
#print(sos2200)

# ===== BANDPASS FILTER FUNCTION =====
def bandpass(lowcut, highcut, fs, order=4):
    nyq = 0.5 * fs
    low = lowcut / nyq
    high = highcut / nyq
    b, a = butter(order, [low, high], btype='band')
    return b, a

# Design filters (wide enough to tolerate drift)
b1200, a1200 = bandpass(LOW_TONE - 200, LOW_TONE + 200, SAMPLE_RATE)
b2200, a2200 = bandpass(HIGH_TONE - 200, HIGH_TONE + 200, SAMPLE_RATE)

# Apply filters
f1200 = lfilter(b1200, a1200, data)
f2200 = lfilter(b2200, a2200, data)

# ===== ENVELOPE DETECTION =====
def envelope(signal, alpha=0.01):
    env = np.zeros_like(signal)
    for i in range(1, len(signal)):
        env[i] = (1 - alpha) * env[i-1] + alpha * abs(signal[i])
    return env

env1200 = envelope(f1200)
env2200 = envelope(f2200)

# Metric (soft decision)
metric = env2200 - env1200

prev = 0
threshold = 0.1  # Adjust as needed
timer = 0
time_to_measure = SAMPLE_RATE / BAUD_RATE / 2  # Half bit duration
for i in range(len(metric)):
    if metric[i] > threshold and prev <= threshold:
        timer = 0
        print(f"edge: {i}\n")
    elif metric[i] <= -threshold and prev > -threshold:
        timer = 0
        print(f"edge: {i}\n")
    
    if timer >= time_to_measure:
        if metric[i] > threshold:
            print(f"1 at {i}\n")
        elif metric[i] < -threshold:
            print(f"0 at {i}\n")
        timer = -time_to_measure  # Prevent multiple detections until next bit period
    timer += 1
    prev = metric[i]
print()  # Newline after output

# ===== TIME AXIS =====
t = np.arange(len(data)) / SAMPLE_RATE

# ===== PLOTTING =====
plt.figure(figsize=(12, 8))

# Raw signal
plt.subplot(4, 1, 1)
plt.plot(t, data)
plt.title("Raw Signal")

# Bandpass outputs
plt.subplot(4, 1, 2)
plt.plot(t, f1200, label="1200 Hz")
plt.plot(t, f2200, label="2200 Hz")
plt.legend()
plt.title("Bandpass Outputs")

# Envelopes
plt.subplot(4, 1, 3)
plt.plot(t, env1200, label="Env 1200")
plt.plot(t, env2200, label="Env 2200")
plt.legend()
plt.title("Envelopes")

# Metric
plt.subplot(4, 1, 4)
plt.plot(t, metric)
plt.axhline(0, linestyle='--')
plt.title("Metric (2200 - 1200)")

plt.tight_layout()
plt.savefig("output.png", dpi=150)
print("Saved to output.png")
