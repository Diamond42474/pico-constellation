#!/bin/bash

# Launcher for the Pico Constellation recorder host tool.
# Run from this directory: ./record.sh

VENV_DIR=".venv"

if [ ! -d "$VENV_DIR" ]; then
    echo "Creating virtual environment..."
    python3 -m venv "$VENV_DIR"
fi

source "$VENV_DIR/bin/activate"

echo "Installing requirements..."
"$VENV_DIR/bin/python" -m pip install -r requirements.txt

echo ""
echo "Available serial ports:"
"$VENV_DIR/bin/python" -c "import serial.tools.list_ports as lp; ports = lp.comports(); print('\n'.join('  ' + p.device + '  |  ' + p.description for p in ports) if ports else '  (none found)')"
echo ""

echo "Starting recorder..."
python3 record.py "$@"