# Include directories
include_directories(
    include
)

# Source files
add_executable(${PROJECT_NAME}
    src/main.c
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    # Standard
    pico_stdlib

    # Device Drivers
    nrf24_driver

    # Constellation
    peregrine-constellation
)

# Enable USB output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Enable extra outputs
pico_add_extra_outputs(${PROJECT_NAME})