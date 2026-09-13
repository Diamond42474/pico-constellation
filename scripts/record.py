import csv
import os
import queue
import struct
import threading
import time
import zlib
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

ADC_SAMPLE_RATE = 48000

FRAME_MAGIC = 0xA5C3F17E
FRAME_MAGIC_BYTES = struct.pack("<I", FRAME_MAGIC)
FRAME_SAMPLE_COUNT = 1024
FRAME_HEADER_FORMAT = "<IHH"
FRAME_HEADER_SIZE = struct.calcsize(FRAME_HEADER_FORMAT)
FRAME_CRC_SIZE = 4
FRAME_SIZE = FRAME_HEADER_SIZE + (FRAME_SAMPLE_COUNT * 2) + FRAME_CRC_SIZE

MAX_DATAPOINTS = 4800

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
            ])
            self._target_count = target_count
            self._sample_count = 0
            self._active = True
            self._last_message = f"Recording to {path.name}"
            return path

    def write_samples(self, samples):
        """Write one received frame, stopping at the requested sample count."""

        with self._lock:
            if not self._active:
                return

            remaining = self._target_count - self._sample_count
            samples_to_write = samples[:remaining]
            first_timestamp = time.time() - (
                (len(samples_to_write) - 1) / ADC_SAMPLE_RATE
            )
            self._writer.writerows(
                (first_timestamp + (index / ADC_SAMPLE_RATE), sample)
                for index, sample in enumerate(samples_to_write)
            )
            self._sample_count += len(samples_to_write)

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


def read_frame(ser, buffer):
    """
    Read, synchronize, and validate the next complete sample frame.

    Frame format:

        uint32 magic (0xA5C3F17E)
        uint16 sequence
        uint16 sample_count (1024)
        uint16 samples[1024]
        uint32 crc32

    The CRC covers everything after magic through the sample payload. Invalid
    frames are ignored and the reader scans again for the next frame marker.
    """

    while True:
        if len(buffer) < len(FRAME_MAGIC_BYTES):
            data = ser.read(4096)

            if not data:
                raise serial.SerialException("Serial device disconnected")

            buffer.extend(data)

        magic_index = buffer.find(FRAME_MAGIC_BYTES)

        if magic_index == -1:
            # Preserve a partial 32-bit marker for the next read.
            if len(buffer) > len(FRAME_MAGIC_BYTES) - 1:
                del buffer[:-(len(FRAME_MAGIC_BYTES) - 1)]
            continue

        if magic_index > 0:
            print(f"Discarding {magic_index} bytes before frame marker")
            del buffer[:magic_index]

        while len(buffer) < FRAME_SIZE:
            data = ser.read(FRAME_SIZE - len(buffer))

            if not data:
                raise serial.SerialException("Serial device disconnected")

            buffer.extend(data)

        frame = bytes(buffer[:FRAME_SIZE])
        magic, sequence, sample_count = struct.unpack_from(
            FRAME_HEADER_FORMAT,
            frame,
        )
        expected_crc = struct.unpack_from("<I", frame, FRAME_SIZE - FRAME_CRC_SIZE)[0]
        actual_crc = zlib.crc32(frame[4:-FRAME_CRC_SIZE]) & 0xFFFFFFFF

        if (
            magic != FRAME_MAGIC
            or sample_count != FRAME_SAMPLE_COUNT
            or actual_crc != expected_crc
        ):
            print("Discarding invalid sample frame")
            # Retain the rest of this potential frame for marker resynchronization.
            del buffer[:1]
            continue

        del buffer[:FRAME_SIZE]
        samples = struct.unpack_from(
            f"<{FRAME_SAMPLE_COUNT}H",
            frame,
            FRAME_HEADER_SIZE,
        )
        return sequence, samples


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
        previous_sequence = None

        try:
            while True:
                sequence, samples = read_frame(ser, buffer)

                if (
                    previous_sequence is not None
                    and sequence != (previous_sequence + 1) & 0xFFFF
                ):
                    print(
                        "Frame sequence gap: "
                        f"expected {(previous_sequence + 1) & 0xFFFF}, "
                        f"received {sequence}"
                    )
                previous_sequence = sequence

                # Recording receives validated frame payloads independent of
                # the graph's decimation rate.
                recording.write_samples(samples)

                for sample in samples:
                    if skip_index == GRAPH_SKIP:
                        graph_queue.put(sample)
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
    """Display raw ADC samples and recording controls in real time."""

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
    data = []

    for i, name in enumerate(["Raw ADC sample"]):
        plot = graph_widget.addPlot(row=i, col=0, title=name)

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

            data.append(record)

            if len(data) > MAX_DATAPOINTS:
                del data[:-MAX_DATAPOINTS]

            plots[0].setData(data)

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
