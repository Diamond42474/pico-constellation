#!/bin/bash

# Launcher for the Pico Constellation recorder GUI.
# Run from this directory: ./gui.sh
# Works on Linux, macOS, and Windows (git-bash).

VENV_DIR=".venv"

PYTHON=""

for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 \
        && "$candidate" --version >/dev/null 2>&1; then
        PYTHON="$candidate"
        break
    fi
done

if [ -z "$PYTHON" ]; then
    echo ""
    echo "Python 3 is not installed or not on PATH."
    echo "Install it (e.g. 'sudo apt install python3' on Debian/Ubuntu,"
    echo "https://www.python.org/downloads/ on Windows/macOS) and try again."
    exit 1
fi

if [ ! -d "$VENV_DIR" ]; then
    echo "Creating virtual environment..."
    "$PYTHON" -m venv "$VENV_DIR"
fi

# Locate the venv interpreter: bin/ on Linux/macOS, Scripts/ on Windows.
if [ -f "$VENV_DIR/bin/python" ]; then
    VENV_PY="$VENV_DIR/bin/python"
elif [ -f "$VENV_DIR/Scripts/python.exe" ]; then
    VENV_PY="$VENV_DIR/Scripts/python.exe"
else
    echo ""
    echo "The $VENV_DIR directory has no Python interpreter."
    echo "Delete it and run this script again."
    exit 1
fi

echo "Installing requirements..."
"$VENV_PY" -m pip install -r requirements.txt

echo ""
echo "Starting recorder GUI..."
"$VENV_PY" record_gui.py
