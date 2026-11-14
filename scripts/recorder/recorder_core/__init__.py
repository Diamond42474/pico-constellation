"""Shared library for the Pico Constellation recorder host tool.

Modules:
    protocol  - wire format, CRC, frame parser
    source    - serial connection and reader thread
    session   - timestamped recording files
    plotting  - rolling buffer and display decimation
"""

from recorder_core.protocol import FrameParser, crc16_ccitt
from recorder_core.session import RecordingSession
from recorder_core.source import SerialSource

__all__ = [
    "FrameParser",
    "crc16_ccitt",
    "RecordingSession",
    "SerialSource",
]
