import csv
import os
import queue
import struct
import threading
import time
from datetime import datetime
from pathlib import Path

import serial
import pyqtgraph as pg

from PyQt6.QtWidgets import (
    QApplication,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)


DEVICE = "/dev/ttyACM0"
BAUD_RATE = 115200

ADC_SAMPLE_RATE = 26250

SYNC_WORD = 0xABCD
SYNC_BYTES = struct.pack("<H", SYNC_WORD)

RECORD_SIZE = 7
MAX_DATAPOINTS = 1000

GRAPH_SKIP = 100  # Number of records to skip between graph updates
RECORDED_DATA_DIRECTORY = Path(__file__).parent / "recorded_data"


graph_queue = queue.Queue()


class RecordingManager:
    """Safely write a fixed number of records from the serial reader."""

    def __init__(self):
        self._lock = threading.Lock()
        self._file = None
        self._writer = None
        self._target_count = 0
        self._sample_count = 0
        self._active = False
        self._last_message = "Ready to record."

    def start(self, target_count):
        """Open a new CSV file and begin recording target_count records."""

        with self._lock:
            if self._active:
                raise RuntimeError("A recording is already in progress.")

            RECORDED_DATA_DIRECTORY.mkdir(parents=True, exist_ok=True)
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            path = RECORDED_DATA_DIRECTORY / f"recording_{timestamp}.csv"

            # Avoid replacing a file if recordings are started twice in one second.
            suffix = 1
            while path.exists():
                path = RECORDED_DATA_DIRECTORY / (
                    f"recording_{timestamp}_{suffix}.csv"
                )
                suffix += 1

            self._file = path.open("w", newline="", encoding="utf-8")
            self._writer = csv.writer(self._file)
            self._writer.writerow([
                "timestamp_unix",
                "sample",
                "filtered_1200",
                "filtered_2200",
                "metric",
            ])
            self._target_count = target_count
            self._sample_count = 0
            self._active = True
            self._last_message = f"Recording to {path.name}"
            return path

    def write_record(self, record):
        """Write one incoming record and close the file at the requested count."""

        with self._lock:
            if not self._active:
                return

            self._writer.writerow([time.time(), *record])
            self._sample_count += 1

            if self._sample_count >= self._target_count:
                self._finish_locked("Recording complete")

    def stop(self):
        """Stop a recording early, keeping the samples already captured."""

        with self._lock:
            if self._active:
                self._finish_locked("Recording stopped")

    def status(self):
        """Return a thread-safe snapshot for the GUI."""

        with self._lock:
            return (
                self._active,
                self._sample_count,
                self._target_count,
                self._last_message,
            )

    def _finish_locked(self, message):
        self._file.close()
        self._file = None
        self._writer = None
        self._active = False
        self._last_message = message


recording = RecordingManager()


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

                # Recording receives every incoming record, independent of the
                # graph's decimation rate.
                recording.write_record(record)

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
    """Display the four record fields and recording controls in real time."""

    app = pg.mkQApp("Signal Data")

    win = QWidget()

    win.setWindowTitle("Signal Data")
    win.resize(1200, 800)

    layout = QVBoxLayout(win)
    controls = QHBoxLayout()

    controls.addWidget(QLabel("Samples to record:"))
    sample_count = QSpinBox()
    sample_count.setRange(1, 10_000_000)
    sample_count.setValue(10_000)
    sample_count.setSingleStep(1_000)
    controls.addWidget(sample_count)

    start_button = QPushButton("Start recording")
    stop_button = QPushButton("Stop recording")
    stop_button.setEnabled(False)
    controls.addWidget(start_button)
    controls.addWidget(stop_button)

    status_label = QLabel("Ready to record.")
    controls.addWidget(status_label, 1)
    layout.addLayout(controls)

    graph_widget = pg.GraphicsLayoutWidget()
    layout.addWidget(graph_widget, 1)

    plots = []
    data = [[], [], [], []]

    names = [
        "Sample",
        "Filtered 1200",
        "Filtered 2200",
        "Metric"
    ]

    for i, name in enumerate(names):
        plot = graph_widget.addPlot(
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

        active, captured, target, message = recording.status()
        start_button.setEnabled(not active)
        stop_button.setEnabled(active)

        if active:
            status_label.setText(f"Recording: {captured} / {target} samples")
        else:
            status_label.setText(message)

    def start_recording():
        try:
            path = recording.start(sample_count.value())
            status_label.setText(f"Recording to {path.name}")
        except (OSError, RuntimeError) as error:
            status_label.setText(f"Could not start recording: {error}")

    start_button.clicked.connect(start_recording)
    stop_button.clicked.connect(recording.stop)

    # Parent the timer to the window so it stays alive.
    timer = pg.QtCore.QTimer(win)

    timer.timeout.connect(update)

    timer.start(10)

    win.show()

    print("Graph started.")

    app.aboutToQuit.connect(recording.stop)
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
