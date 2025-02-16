#ifndef ADC_FFT_H
#define ADC_FFT_H

#include <stdint.h>
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

// Function Declarations
void adc_fft_init(void);            // Initialize ADC and DMA
void adc_fft_start_sampling(void);  // Start ADC sampling
void adc_fft_perform_fft(void);     // Perform FFT on the sampled data

#endif // ADC_FFT_H