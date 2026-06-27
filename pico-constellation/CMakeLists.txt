# Include directories
include_directories(
    include
    include/communication
    include/drivers
    include/network
    include/ui
    include/utils
)

# Complier optimize for speed
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Ofast")

# Source files
add_executable(${PROJECT_NAME}
	#src/transmitter.c
    #src/receiver.c
    #src/test.c
    src/main.c

    # Drivers
    src/drivers/ad9833.c
    src/drivers/adc_hal.c

    # Network
    src/network/network.c
    src/network/dhcpserver.c
    src/network/dnsserver.c

    # User Interface
    src/ui/ui.c

    # Utils
    src/utils/HAL_time.c

    # BSP
    src/bsp/adc_bsp.c
    src/bsp/dac_bsp.c
    src/bsp/ptt_bsp.c
    src/bsp/time_bsp.c
)

# Link libraries
target_link_libraries(${PROJECT_NAME}
    # Standard
    pico_stdlib
    hardware_i2c
    hardware_adc
    hardware_dma
    hardware_pwm
    hardware_watchdog

    pico_cyw43_arch_lwip_threadsafe_background

    # Constellation
    peregrine-constellation

    # Hardware drivers
    fhdm-ad9833-pico

    # Utility libraries
    c-logger
)

# add_definitions(-DFIXED_POINT=16 -Dkiss_fft_scalar=uint16_t)
add_compile_definitions(${PROJECT_NAME} 
PRIVATE 
    #LWIP_PROVIDE_ERRNO=1
)

target_compile_options(${PROJECT_NAME} PRIVATE -Ofast)

# Enable USB output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Enable extra outputs
pico_add_extra_outputs(${PROJECT_NAME})

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "=== Memory Usage ==="
    COMMAND arm-none-eabi-size -B ${PROJECT_NAME}.elf
)