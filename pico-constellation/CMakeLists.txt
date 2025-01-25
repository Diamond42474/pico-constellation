# Include directories
include_directories(
    include
    include/drivers
)

# Source files
add_executable(${PROJECT_NAME}
    src/main.c

    # Drivers
    src/drivers/ad9833.c
    src/drivers/mcp4725.c
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    # Standard
    pico_stdlib
    hardware_i2c

    # Constellation
    peregrine-constellation

    # Hardware drivers
    fhdm-ad9833-pico
)

# Enable USB output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Enable extra outputs
pico_add_extra_outputs(${PROJECT_NAME})