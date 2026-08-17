import serial
import struct

PORT = "/dev/ttyACM1"       # Change this to your Pico's COM port
BAUD = 115200       # Ignored by USB CDC, but required by pyserial
RECORD_SIZE = 5

ser = serial.Serial(PORT, BAUD, timeout=1)

print("Waiting for data...")

buffer = bytearray()

try:
    while True:
        data = ser.read(4096)

        if data:
            buffer.extend(data)

        while len(buffer) >= RECORD_SIZE:
            record = buffer[:RECORD_SIZE]
            del buffer[:RECORD_SIZE]

            sample, filtered_1200, filtered_2200, metric = struct.unpack(
                "<HBBB", record
            )

            print(
                f"sample={sample:5d} "
                f"1200={filtered_1200:3d} "
                f"2200={filtered_2200:3d} "
                f"metric={metric:3d}"
            )

except KeyboardInterrupt:
    print("\nStopping...")

finally:
    ser.close()