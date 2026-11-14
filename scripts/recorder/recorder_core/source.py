"""Serial source: connects to the Pico and streams parsed frames.

SerialSource owns the port, a reader thread, and the frame parser.
Parsed frames are dispatched to subscribers on the reader thread:

    source.subscribe(lambda frame: ...)   # (seq, idx, dropped, bytes)

Consumers that must stay on their own thread (e.g. a GUI main loop)
should push frames onto a queue in the callback and drain it later.
"""

import threading
import time

from recorder_core.protocol import FrameParser, READ_SIZE


def list_serial_ports():
    """Return available serial port device names (e.g. ['COM3'])."""

    import serial.tools.list_ports

    return [
        p.device for p in serial.tools.list_ports.comports()
    ]


def open_serial(port: str, baudrate: int = 115200, should_stop=None):
    """Open the serial port, waiting for the device to appear.

    Retries every 2 seconds until the port opens or should_stop is set.
    Returns the open serial.Serial, or None if stopped while waiting.
    """

    import serial

    print(f"Opening {port} ...")

    while True:

        if should_stop is not None and should_stop.is_set():
            return None

        try:
            ser = serial.Serial(
                port=port,
                baudrate=baudrate,
                timeout=1.0,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False,
            )

            print(f"Connected to {port}")
            return ser

        except (serial.SerialException, OSError) as e:

            print(f"Could not open {port}: {e}")
            print("Is the Pico plugged in? Retrying every 2 seconds.")

            # Sleep in short steps so stop() is responsive.
            for _ in range(10):
                if should_stop is not None and should_stop.is_set():
                    return None
                time.sleep(0.2)


class SerialSource:
    """Reads frames from a serial port and dispatches them to subscribers."""

    def __init__(self, port: str, baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate

        self._parser = FrameParser()
        self._subscribers = []
        self._stop = threading.Event()
        self._reader_done = threading.Event()
        self._thread = None
        self._ser = None
        self.errors = []

    @property
    def stats(self):
        """Live parser statistics (dict)."""

        return self._parser.stats

    @property
    def connected(self):
        return self._ser is not None

    @property
    def finished(self):
        """True once the reader has stopped (or been stopped)."""

        return self._stop.is_set() or self._reader_done.is_set()

    def subscribe(self, callback):
        """Register a callback invoked with each parsed frame."""

        self._subscribers.append(callback)

    def connect(self):
        """Open the port (blocks, retrying until it appears or stop()).

        Returns True if connected, False if cancelled via stop().
        """

        self._ser = open_serial(
            self.port,
            baudrate=self.baudrate,
            should_stop=self._stop,
        )

        return self._ser is not None

    def start(self):
        """Start the reader thread (call connect() first)."""

        if self._ser is None:
            raise RuntimeError("not connected; call connect() first")

        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def stop(self):
        """Stop the reader thread and close the port."""

        self._stop.set()

        if self._thread is not None:
            self._thread.join(timeout=3.0)

            if self._thread.is_alive():
                self.errors.append("Reader thread did not stop cleanly.")

        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:
                pass

            self._ser = None

        self._reader_done.set()

    def _run(self):
        empty_streak = 0

        try:
            while not self._stop.is_set():

                data = self._ser.read(READ_SIZE)

                if not data:

                    empty_streak += 1

                    if empty_streak >= 5:
                        self.errors.append(
                            "No data for approximately 5 seconds "
                            "(device disconnected or recording stopped)."
                        )
                        break

                    continue

                empty_streak = 0

                for frame in self._parser.feed(data):
                    for callback in list(self._subscribers):
                        callback(frame)

        except Exception as e:
            self.errors.append(str(e))

        finally:
            # Close the port so `connected` reflects reality after an
            # unplug (stop() skips it if we got here first).
            try:
                self._ser.close()
            except Exception:
                pass

            self._ser = None
            self._reader_done.set()
