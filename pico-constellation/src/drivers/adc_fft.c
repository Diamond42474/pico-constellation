#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "kiss_fft.h"
#include <math.h>
#include <stdio.h>

#define ADC_CHANNEL 0
#define SAMPLE_RATE 16000
#define FFT_SIZE 512
#define FIXED_POINT_SCALE (1 << 16)  // Scale factor for 16-bit fixed-point (2^12 for range)

static uint16_t sample_buffer[FFT_SIZE];
static int dma_channel;

void adc_fft_init(void)
{
    stdio_init_all();
    adc_init();
    adc_gpio_init(26);
    adc_select_input(ADC_CHANNEL);

    adc_fifo_setup(true, true, 1, false, false); // Set FIFO threshold to 4 for efficiency

    // Corrected ADC clock divider calculation
    adc_set_clkdiv((uint16_t)((48000000 / SAMPLE_RATE) - 1));

    dma_channel = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_channel);
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, DREQ_ADC);

    dma_channel_configure(
        dma_channel, &cfg, sample_buffer, &adc_hw->fifo, FFT_SIZE, false);
}

void adc_fft_start_sampling(void)
{
    adc_fifo_drain(); // Clear the FIFO buffer before starting the ADC
    adc_run(true);    // Start the ADC conversion
    dma_channel_set_write_addr(dma_channel, sample_buffer, true);
}

void adc_fft_perform_fft(void)
{
    // Wait until the DMA transfer is complete
    dma_channel_wait_for_finish_blocking(dma_channel);

    // Allocate memory for the FFT
    kiss_fft_cfg cfg = kiss_fft_alloc(FFT_SIZE, 0, NULL, NULL);
    if (!cfg)
    {
        printf("Error: Failed to allocate memory for FFT.\n");
        return;
    }

    kiss_fft_cpx fft_in[FFT_SIZE];
    kiss_fft_cpx fft_out[FFT_SIZE];

    // Compute the DC offset (mean value of the signal)
    int32_t mean = 0;
    for (int i = 0; i < FFT_SIZE; i++)
    {
        mean += sample_buffer[i];
    }
    mean /= FFT_SIZE;  // Calculate the mean (DC offset)

    // Remove DC bias and scale the data to fixed-point (16-bit)
    for (int i = 0; i < FFT_SIZE; i++)
    {
        // Use a smaller scale factor to fit within 16-bit range
        fft_in[i].r = (int16_t)((((int32_t)(sample_buffer[i] - mean)) * FIXED_POINT_SCALE) / 1024);  // Scale to 16-bit fixed-point
        fft_in[i].i = 0;  // Imaginary part is zero for real inputs
    }

    // Perform the FFT
    kiss_fft(cfg, fft_in, fft_out);

    // Find the dominant frequency and its magnitude in fixed-point
    int32_t max_magnitude = 0;
    int max_index = 0;
    for (int i = 0; i < FFT_SIZE / 2; i++)
    {
        int32_t real = fft_out[i].r;
        int32_t imag = fft_out[i].i;
        int32_t magnitude = (real * real) + (imag * imag);  // Use integer math for magnitude (squared)

        if (magnitude > max_magnitude)
        {
            max_magnitude = magnitude;
            max_index = i;
        }
    }

    // Calculate the dominant frequency from the index
    float dominant_frequency = (float)max_index * SAMPLE_RATE / FFT_SIZE;

    // Compute the amplitude in dB (use integer-based magnitude)
    int32_t amplitude = max_magnitude >> 16;  // Scale down magnitude for display (after fixed-point scale)
    float amplitude_db = 20.0f * log10f((float)amplitude);

    // Display results
    if (max_index == 0)
    {
        printf("Warning: DC component is dominant, check input signal.\n");
    }
    printf("Dominant Frequency: %.2f Hz, Amplitude: %.2f dB\n", dominant_frequency, amplitude_db);

    // Free the FFT configuration memory
    kiss_fft_free(cfg);
}
