import serial
import struct

ser = serial.Serial('/dev/ttyACM0', timeout=10)

# Read header (8 bytes)
header = ser.read(8)
sample_rate, total_samples = struct.unpack('<II', header)

print(f"Sample rate: {sample_rate}")
print(f"Total samples: {total_samples}")

# Read sample data
raw = ser.read(total_samples * 2)

with open("capture.raw", "wb") as f:
    f.write(raw)

print("Saved to capture.raw")