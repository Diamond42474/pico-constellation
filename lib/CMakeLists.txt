# Add the peregrine-constellation library
add_subdirectory(peregrine-constellation/peregrine-constellation)

# Add ad9833 driver
add_subdirectory(ad9833-pico/lib)

# Add C-Logger library
set(C_LOGGER_ENABLE_TESTS OFF)
add_subdirectory(c-logger)