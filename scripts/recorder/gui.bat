@echo off
rem Launcher for the Pico Constellation recorder GUI.
rem Usage: gui.bat

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
echo Starting recorder GUI...
"%VENV_DIR%\Scripts\python.exe" record_gui.py

