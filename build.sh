#!/bin/bash

export PICO_SDK_PATH=~/Documents/git/pico-sdk
export PICOTOOL_PATH=~/Documents/git/pico-tool

# Create the build directory
mkdir build

# Change to the build directory
cd build

# Build the project
cmake ..
make