import os
import time
import struct
import queue
import threading

import serial
import pyqtgraph as pg

from PyQt6.QtWidgets import QApplication


DEVICE = "/dev/ttyACM0"
BAUD_RATE = 115200

ADC_SAMPLE_RATE = 48000

SYNC_WORD = 0xABCD
SYNC_BYTES = struct.pack("<H", SYNC_WORD)

RECORD_SIZE = 7
MAX_DATAPOINTS = 1000

GRAPH_SKIP = 100  # Number of records to skip between graph updates


# Global queue containing:
# (sample, filtered_1200, filtered_2200, metric)
record_queue = queue.Queue()
graph_queue = queue.Queue()


def wait_for_device():
    """Block until the serial device exists."""

    while not os.path.exists(DEVICE):
        time.sleep(0.1)

    return DEVICE


def connect_serial():
    """Wait for and open the serial device."""

    while True:
        wait_for_device()

        try:
            ser = serial.Serial(
                DEVICE,
                BAUD_RATE,
                timeout=1
            )

            print(f"Connected to {DEVICE}")
            return ser

        except (serial.SerialException, OSError) as e:
            print(f"Failed to connect: {e}")
            time.sleep(0.5)


def read_record(ser, buffer):
    """
    Read and synchronize to the next complete record.

    Record format:

        uint16 sync
        uint16 sample
        uint8  filtered_1200
        uint8  filtered_2200
        uint8  metric

    Total: 7 bytes

    The function searches for SYNC_WORD in the byte stream and
    discards any bytes before it.
    """

    while True:

        # Read more data if we don't have enough to search for
        # the sync word.
        if len(buffer) < 2:
            data = ser.read(64)

            if not data:
                raise serial.SerialException(
                    "Serial device disconnected"
                )

            buffer.extend(data)

        # Search for the sync word.
        sync_index = buffer.find(SYNC_BYTES)

        if sync_index == -1:
            # No sync word found.

            # Keep the final byte in case it is the first half
            # of the sync word.
            if len(buffer) > 1:
                del buffer[:-1]

            continue

        # Discard anything before the sync word.
        if sync_index > 0:
            del buffer[:sync_index]

        # We now know buffer[0:2] is the sync word.
        # Wait until the complete record is available.
        while len(buffer) < RECORD_SIZE:
            data = ser.read(RECORD_SIZE - len(buffer))

            if not data:
                raise serial.SerialException(
                    "Serial device disconnected"
                )

            buffer.extend(data)

        # Extract the complete record.
        record = bytes(buffer[:RECORD_SIZE])
        del buffer[:RECORD_SIZE]

        sync, sample, filtered_1200, filtered_2200, metric = (
            struct.unpack("<HHBBB", record)
        )

        # This should always be true because we found SYNC_BYTES,
        # but verify it anyway.
        if sync != SYNC_WORD:
            continue

        return sample, filtered_1200, filtered_2200, metric


def serial_reader():
    """
    Continuously read records from the serial device.

    If the device disconnects, close the connection and
    return to waiting for it to reconnect.
    """

    while True:
        ser = connect_serial()

        # Persistent byte buffer used for synchronization.
        buffer = bytearray()

        skip_index = 0

        try:
            while True:
                record = read_record(ser, buffer)

                # Uncomment if you want every record in the queue.
                # record_queue.put(record)

                if skip_index == GRAPH_SKIP:
                    graph_queue.put(record)
                    skip_index = 0

                skip_index += 1

        except (serial.SerialException, OSError) as e:
            print(f"Disconnected: {e}")

        finally:
            try:
                ser.close()
            except Exception:
                pass

            print("Waiting for device to reconnect...")


def graph_data():
    """Display the four record fields in real time."""

    app = pg.mkQApp("Signal Data")

    win = pg.GraphicsLayoutWidget()

    win.setWindowTitle("Signal Data")
    win.resize(1200, 800)

    plots = []
    data = [[], [], [], []]

    names = [
        "Sample",
        "Filtered 1200",
        "Filtered 2200",
        "Metric"
    ]

    for i, name in enumerate(names):
        plot = win.addPlot(
            row=i,
            col=0,
            title=name
        )

        plot.showGrid(x=True, y=True)

        curve = plot.plot()

        plots.append(curve)

    def update():
        """Consume all currently available records."""

        while True:
            try:
                record = graph_queue.get_nowait()

            except queue.Empty:
                break

            sample, filtered_1200, filtered_2200, metric = record

            values = [
                sample,
                filtered_1200,
                filtered_2200,
                metric
            ]

            for i, value in enumerate(values):
                data[i].append(value)

                if len(data[i]) > MAX_DATAPOINTS:
                    data[i] = data[i][-MAX_DATAPOINTS:]

                plots[i].setData(data[i])

    # Parent the timer to the window so it stays alive.
    timer = pg.QtCore.QTimer(win)

    timer.timeout.connect(update)

    timer.start(10)

    win.show()

    print("Graph started.")

    app.exec()


def main():
    reader_thread = threading.Thread(
        target=serial_reader,
        daemon=True
    )

    reader_thread.start()

    # Qt must run on the main thread.
    graph_data()


if __name__ == "__main__":
    main()