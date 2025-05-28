# Add the peregrine-constellation library
add_subdirectory(peregrine-constellation/peregrine-constellation)

# Add ad9833 driver
add_subdirectory(ad9833-pico/lib)

# Add kiss_fft library
set(KISSFFT_TEST OFF)
set(KISSFFT_TOOLS OFF)
set(KISSFFT_DATATYPE int16_t)
set(FIXED_POINT 16)
add_subdirectory(kissfft)