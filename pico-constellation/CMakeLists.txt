# Include directories
include_directories(
    include
    include/communication
    include/drivers
    include/utils
)

# Source files
add_executable(${PROJECT_NAME}
    src/transmitter.c

    # Communications
    src/communication/communication_interface.c

    # Drivers
    src/drivers/ad9833.c
    src/drivers/adc_hal.c

    # Utils
    src/utils/HAL_time.c
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    # Standard
    pico_stdlib
    hardware_i2c
    hardware_adc
    hardware_dma

    # Constellation
    peregrine-constellation

    # Hardware drivers
    fhdm-ad9833-pico

    # Utility libraries
    c-logger
)

# add_definitions(-DFIXED_POINT=16 -Dkiss_fft_scalar=uint16_t)
# add_compile_definitions(${PROJECT_NAME} 
# PRIVATE 
#     KISSFFT_DATATYPE=int16_t 
#     FIXED_POINT=16 
# )

# Enable USB output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Enable extra outputs
pico_add_extra_outputs(${PROJECT_NAME})