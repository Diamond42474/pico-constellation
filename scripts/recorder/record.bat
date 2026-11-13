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

echo Starting recorder...
"%VENV_DIR%\Scripts\python.exe" record.py %*

