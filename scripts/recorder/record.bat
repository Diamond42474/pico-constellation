@echo off
rem Launcher for the Pico Constellation recorder host tool.
rem Usage: record.bat --port COM5 [--duration 30] [--out capture.raw]

set "VENV_DIR=.venv"

where python >nul 2>nul
if errorlevel 1 goto no_python

python --version >nul 2>nul
if errorlevel 1 goto no_python

goto python_ok

:no_python
echo.
echo Python 3 is not installed or not on PATH.
echo Install it from https://www.python.org/downloads/ and make sure
echo "Add python.exe to PATH" is checked during setup, then try again.
echo.
exit /b 1

:python_ok

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

