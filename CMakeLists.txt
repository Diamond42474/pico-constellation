cmake_minimum_required(VERSION 3.13)

set(PROJECT_NAME pico-constellation)

# Specify C standard
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)

# Import the Pico SDK
set(PICO_BOARD pico2_w)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

# Set the project name and version
project(${PROJECT_NAME} 
    VERSION 0.1.0 
    LANGUAGES C CXX
)

# Initialize the SDK
pico_sdk_init()

# Add libraries
add_subdirectory(lib)

# Add the main project
add_subdirectory(pico-constellation)