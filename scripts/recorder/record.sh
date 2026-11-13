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
python3 -m pip install -r requirements.txt

echo "Starting recorder..."
python3 record.py "$@"