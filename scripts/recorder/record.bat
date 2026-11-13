@echo off
rem Launcher for the Pico Constellation recorder host tool.
rem Usage: record.bat --port COM5 [--duration 30] [--out capture.raw]

set "VENV_DIR=.venv"

if not exist "%VENV_DIR%\" (
    echo Creating virtual environment...
    python -m venv "%VENV_DIR%"
)

echo Installing requirements...
"%VENV_DIR%\Scripts\python.exe" -m pip install -r requirements.txt

echo.
echo Available serial ports:
"%VENV_DIR%\Scripts\python.exe" -c "import serial.tools.list_ports as lp; ports = lp.comports(); print('\n'.join('  ' + p.device + '  |  ' + p.description for p in ports) if ports else '  (none found)')"
echo.

echo Starting recorder...
"%VENV_DIR%\Scripts\python.exe" record.py %*

