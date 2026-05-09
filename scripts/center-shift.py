import numpy as np

sample_rate = 70400

data_u16 = np.fromfile("capture.raw", dtype=np.uint16)

# Convert to signed 16-bit centered around 0
data_i16 = (data_u16.astype(np.int32) - 2048).astype(np.int16)

# Save the centered data to a .wav file
from scipy.io import wavfile
wavfile.write("capture_centered2.wav", sample_rate, data_i16)