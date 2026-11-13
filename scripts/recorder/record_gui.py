#!/usr/bin/env python3
"""
GUI for the Pico Constellation recorder.

The ADC stream is plotted live as soon as the selected port connects —
recording to disk is controlled separately with the Start/Stop button.
Captures are saved as timestamped files in the git-ignored recordings/
folder.

Requires: tkinter (stdlib), numpy, matplotlib, pyserial.

    python record_gui.py
"""

import queue
import threading
import time

import tkinter as tk
from tkinter import ttk

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure

from recorder_core.plotting import RollingBuffer, decimate_minmax
from recorder_core.session import RecordingSession
from recorder_core.source import SerialSource, list_serial_ports

SAMPLE_RATE = 44100
WINDOW_SECONDS = 5.0
DISPLAY_POINTS = 2000
TICK_MS = 50
PLOT_INTERVAL = 0.2      # seconds; draws are ~40 ms here, so not every tick
STATUS_INTERVAL = 1.0
RECONNECT_INTERVAL = 2.0
ADC_MAX = 4095


class RecorderGui:
    def __init__(self, root):
        self.root = root
        root.title("Pico Constellation Recorder")
        root.geometry("960x620")
        root.minsize(760, 480)

        self.source = None
        self.session = None
        self.frames = queue.Queue()
        self.buffer = RollingBuffer(int(WINDOW_SECONDS * SAMPLE_RATE * 2))
        self.recording = False
        self.total_samples = 0
        self.record_start = None
        self.stream_start = None
        self._last_status = 0.0
        self._last_plot = 0.0
        self._last_reconnect = 0.0
        self._connecting = False
        self._connected_event = threading.Event()

        # -------------------------------------------------------------
        # Controls
        # -------------------------------------------------------------

        controls = ttk.Frame(root, padding=(10, 8))
        controls.pack(fill="x")

        ttk.Label(controls, text="Port:").pack(side="left")

        self.port_var = tk.StringVar()
        self.port_box = ttk.Combobox(
            controls, textvariable=self.port_var, width=14, state="readonly"
        )
        self.port_box.pack(side="left", padx=6)
        self.port_box.bind(
            "<<ComboboxSelected>>", lambda event: self.connect_to_selected()
        )

        ttk.Button(
            controls, text="Refresh", command=self.refresh_ports, width=9
        ).pack(side="left")

        self.toggle_btn = ttk.Button(
            controls, text="Start Recording", command=self.toggle_recording,
            width=18,
        )
        self.toggle_btn.pack(side="left", padx=(20, 0))

        # -------------------------------------------------------------
        # Live plot
        # -------------------------------------------------------------

        self.figure = Figure(figsize=(9.5, 4.6), dpi=100)
        self.ax = self.figure.add_subplot(111)
        self.ax.set_title(f"Waveform (last {WINDOW_SECONDS:.0f} s)")
        self.ax.set_xlabel("seconds")
        self.ax.set_ylabel("ADC value")
        self.ax.set_ylim(0, ADC_MAX)
        self.ax.set_xlim(-WINDOW_SECONDS, 0)
        self.line_min, = self.ax.plot([], [], lw=0.8, color="#1f77b4")
        self.line_max, = self.ax.plot([], [], lw=0.8, color="#1f77b4")
        self.figure.tight_layout()

        self.canvas = FigureCanvasTkAgg(self.figure, master=root)
        self.canvas.get_tk_widget().pack(
            fill="both", expand=True, padx=10, pady=(0, 6)
        )

        # -------------------------------------------------------------
        # Status bar
        # -------------------------------------------------------------

        self.status = tk.StringVar(value="Idle — pick a port")
        ttk.Label(
            root, textvariable=self.status, anchor="w", padding=(10, 6)
        ).pack(fill="x", side="bottom")

        self.refresh_ports()
        root.protocol("WM_DELETE_WINDOW", self.on_close)
        self.root.after(TICK_MS, self.tick)

    # -----------------------------------------------------------------
    # Setup helpers
    # -----------------------------------------------------------------

    def refresh_ports(self):
        ports = list_serial_ports()
        self.port_box["values"] = ports

        current = self.port_var.get()
        if ports:
            if current not in ports:
                self.port_var.set(ports[0])
        else:
            self.port_var.set("")

        # A re-scan may mean the device (re)appeared: (re)connect.
        self.connect_to_selected()

    def set_status(self, text):
        self.status.set(text)

    # -----------------------------------------------------------------
    # Connection (the stream runs continuously, recording is separate)
    # -----------------------------------------------------------------

    def connect_to_selected(self):
        port = self.port_var.get()
        if not port:
            self.set_status("Idle — pick a port")
            return

        if self.recording:
            self.stop_recording(note="Port changed")

        if self.source is not None:
            self.source.stop()
            self.source = None

        # Drop anything left over from the previous stream.
        while True:
            try:
                self.frames.get_nowait()
            except queue.Empty:
                break

        self.buffer = RollingBuffer(int(WINDOW_SECONDS * SAMPLE_RATE * 2))
        self.total_samples = 0
        self.stream_start = None
        self._connected_event.clear()

        self.source = SerialSource(port)
        self.source.subscribe(lambda frame: self.frames.put(frame))
        self._connecting = True

        worker = threading.Thread(target=self._connect_and_start, daemon=True)
        worker.start()

        self.set_status(f"Connecting to {port} ...")

    def _connect_and_start(self):
        source = self.source
        if source is None:
            return

        connected = source.connect()

        if self.source is not source:
            # Superseded (port changed / refreshed while connecting).
            if connected:
                source.stop()
            return

        self._connecting = False

        if not connected:
            return                      # cancelled via stop()

        source.start()
        self.stream_start = time.monotonic()
        self._connected_event.set()

    def _on_connected(self):
        if self.source is None or not self.source.connected:
            return

        self._last_status = time.monotonic()
        self.set_status("Streaming")

    # -----------------------------------------------------------------
    # Recording control (file output only; the stream keeps running)
    # -----------------------------------------------------------------

    def toggle_recording(self):
        if self.recording:
            self.stop_recording()
        else:
            self.start_recording()

    def start_recording(self):
        if self.source is None or not self.source.connected:
            self.set_status("Not connected yet — waiting for the stream")
            return

        self.session = RecordingSession.open()
        self.recording = True
        self.record_start = time.monotonic()
        self.total_samples = 0
        self._last_status = self.record_start
        self.toggle_btn.configure(text="Stop Recording")
        self.set_status(f"Recording to {self.session.path.name} ...")

    def stop_recording(self, note=None):
        summary = None

        if self.session is not None:
            self.session.close()
            summary = self.session.summary()
            self.session = None

        self.recording = False
        self.toggle_btn.configure(text="Start Recording")

        msg = f"Saved {summary}" if summary else "Stopped."
        if note:
            msg = f"{note} — {msg}"
        self.set_status(msg)

    # -----------------------------------------------------------------
    # Live view
    # -----------------------------------------------------------------

    def tick(self):
        try:
            self._tick()
        except Exception:
            # Never let a bad frame freeze the GUI; report and keep going.
            import traceback
            traceback.print_exc()

        self.root.after(TICK_MS, self.tick)

    def _tick(self):
        if self._connected_event.is_set():
            self._connected_event.clear()
            self._on_connected()

        got = 0

        while True:
            try:
                _seq, _first, _dropped, samples = self.frames.get_nowait()
            except queue.Empty:
                break

            self.buffer.append(samples)

            if self.session is not None:
                self.session.write(samples)

            self.total_samples += len(samples) // 2
            got += 1

        if got:
            # Drawing is expensive (~40 ms); throttle to PLOT_INTERVAL
            # so timer/button events stay responsive.
            now = time.monotonic()
            if now - self._last_plot >= PLOT_INTERVAL:
                self._last_plot = now
                self.update_plot()

        source = self.source

        if source is not None and source.finished:
            # The device went away (or the read loop died).
            if self.recording:
                self.stop_recording(note="Device disconnected")

        if (source is None or source.finished) and not self._connecting:
            # Auto-reconnect so the stream (and plot) keeps running.
            port = self.port_var.get()
            if port:
                now = time.monotonic()
                if now - self._last_reconnect >= RECONNECT_INTERVAL:
                    self._last_reconnect = now
                    self.connect_to_selected()

        now = time.monotonic()
        if now - self._last_status >= STATUS_INTERVAL:
            self._last_status = now
            self.update_status()

    def update_status(self):
        source = self.source
        now = time.monotonic()

        if self.recording:
            elapsed = now - self.record_start
            rate = self.total_samples / elapsed if elapsed > 0 else 0
            s = source.stats if source else {}
            self.set_status(
                f"Recording {elapsed:.1f} s — "
                f"{self.total_samples:,} samples — "
                f"{rate:.0f} Hz — "
                f"frames {s.get('frames_ok', 0):,} — "
                f"missing {s.get('samples_missing', 0):,} — "
                f"drops {s.get('reported_drops', 0):,} — "
                f"CRC err {s.get('frames_bad_crc', 0):,}"
            )

        elif source is not None and source.connected:
            elapsed = now - self.stream_start if self.stream_start else 0
            rate = self.total_samples / elapsed if elapsed > 0 else 0
            self.set_status(
                f"Streaming {elapsed:.0f} s — "
                f"{self.total_samples:,} samples — "
                f"{rate:.0f} Hz — "
                f"frames {source.stats.get('frames_ok', 0):,} — "
                f"missing {source.stats.get('samples_missing', 0):,} — "
                f"drops {source.stats.get('reported_drops', 0):,} — "
                f"CRC err {source.stats.get('frames_bad_crc', 0):,}"
            )

        elif source is not None and self._connecting:
            self.set_status(f"Connecting to {source.port} ...")

        elif self.port_var.get():
            self.set_status("Disconnected — replug and press Refresh")

        else:
            self.set_status("Idle — pick a port")

    def update_plot(self):
        w = self.buffer.window(int(WINDOW_SECONDS * SAMPLE_RATE))
        if len(w) == 0:
            return

        x, ymin, ymax = decimate_minmax(w, DISPLAY_POINTS)
        xs = (x - len(w)) / SAMPLE_RATE   # seconds relative to newest sample
        span = len(w) / SAMPLE_RATE

        self.line_min.set_data(xs, ymin)
        self.line_max.set_data(xs, ymax)
        self.ax.set_xlim(-span, 0)
        self.canvas.draw_idle()

    # -----------------------------------------------------------------

    def on_close(self):
        self.stop_recording()

        if self.source is not None:
            self.source.stop()
            self.source = None

        self.root.destroy()


def main():
    root = tk.Tk()
    RecorderGui(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
