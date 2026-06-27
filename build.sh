#!/bin/bash

export PICO_SDK_PATH=~/Documents/git/pico-sdk
export PICOTOOL_PATH=~/Documents/git/picotool
export PICOTOOL_FETCH_FROM_GIT_PATH=~/Documents/git/picotool

# Create the build directory
mkdir build

# Change to the build directory
cd build

# Build the project
cmake ..
make

# Memory Usage Report
cd ..
python3 scripts/mem_report.py build/pico-constellation/pico-constellation.elf size.txt