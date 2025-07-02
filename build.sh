#!/bin/bash

export PICO_SDK_PATH=~/Documents/git/pico-sdk
export PICOTOOL_PATH=~/Documents/git/pico-tool
export PICOTOOL_FETCH_FROM_GIT_PATH=~/Documents/git/picotool

# Create the build directory
mkdir build

# Change to the build directory
cd build

# Build the project
cmake ..
make