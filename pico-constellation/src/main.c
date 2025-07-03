#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>
#include <time.h>
#include "c-logger.h"
#include "communication_interface.h"

#include "ad9833.h"
#include "adc_hal.h"
#include "goertzel.h"

#define POWER_THRESHOLD 100000.0f // Power threshold for detecting signals
#define SAMPLE_RATE 2400          // 2400 Hz sample rate
#define BAUD_RATE 8               // bits per second
#define BIT_SAMPLES (SAMPLE_RATE / BAUD_RATE)

static uint16_t adc_samples[BIT_SAMPLES * 8]; // Buffer for ADC samples
static bool adc_samples_ready = false;
static size_t adc_sample_size = 0;

static int data_buffer[100]; // Buffer to store detected data
static int index = 0;        // Current index in the data buffer

void my_adc_callback(size_t size)
{
    printf("ADC callback triggered with size: %zu\n", size);
    adc_samples_ready = true;
    adc_sample_size = size;
    if (size > sizeof(adc_samples) / sizeof(adc_samples[0]))
    {
        LOG_ERROR("ADC sample size exceeds buffer size, truncating");
    }
}

void process_samples(const uint16_t *samples, size_t num_samples)
{
    // Process the ADC samples here
    float power1, power2;
    goertzel_compute_power(samples, num_samples, 1100.0f, SAMPLE_RATE, &power1);
    goertzel_compute_power(samples, num_samples, 2200.0f, SAMPLE_RATE, &power2);

    if (power1 < POWER_THRESHOLD && power2 < POWER_THRESHOLD)
    {
        LOG_DEBUG("No significant signal detected");
        return;
    }

    if (index >= sizeof(data_buffer) - 2)
    {
        LOG_ERROR("Data buffer overflow, resetting index");
        index = 0; // Reset index to prevent overflow
    }
    if (power1 > power2)
    {
        data_buffer[index++] = 0; // 0 for 1100 Hz
    }
    else
    {
        data_buffer[index++] = 1; // 1 for 2200 Hz
    }
    printf("Index: %d\n", index);

    // LOG_DEBUG("Power at 1100 Hz: %.2f, Power at 2200 Hz: %.2f", power1, power2);
}

int main()
{
    int ret = 0;
    stdio_init_all();
    sleep_ms(2000); // Wait for USB to be ready
    log_init(LOG_LEVEL_DEBUG);
    LOG_INFO("Booting Pico Constellation...");

    goertzel_init();
    if (adc_hal_init())
    {
        LOG_FATAL("Failed to initialize ADC HAL");
        ret = -1;
        goto failed;
    }

    if (ad9833_init())
    {
        LOG_FATAL("Failed to initialize AD9833");
        ret = -1;
        goto failed;
    }

    ad9833_set_mode(AD9833_MODE_SINE);

    adc_hal_set_callback(my_adc_callback);
    adc_hal_set_sample_rate(SAMPLE_RATE);
    adc_hal_set_sample_size(BIT_SAMPLES);

    ad9833_set_frequency_hz(2200.0F); // Set initial frequency to 2200 Hz

    if (adc_hal_start())
    {
        LOG_FATAL("Failed to start ADC sampling");
        ret = -1;
        goto failed;
    }

    while (true)
    {
        int num_samples = 0;
        if (adc_samples_ready)
        {
            adc_samples_ready = false;     // Reset the flag
            num_samples = adc_sample_size; // Use the size from the callback
            if (adc_hal_get_samples(adc_samples, sizeof(adc_samples) / sizeof(adc_samples[0]), &num_samples) < 0)
            {
                LOG_ERROR("Failed to get ADC samples");
                continue;
            }
        }

        if (num_samples >= BIT_SAMPLES)
        {
            int bits = num_samples / BIT_SAMPLES;
            for (int i = 0; i < bits; i++)
            {
                process_samples(adc_samples + i * BIT_SAMPLES, BIT_SAMPLES);
            }
        }

        if (index >= 10)
        {
            printf("Received data: ");
            for (int i = 0; i < index; i++)
            {
                printf("%d", data_buffer[i]);
            }
            printf("\n");
            index = 0;
        }
        // ad9833_set_frequency_hz(1100.0F);
        // sleep_ms(1000/ BAUD_RATE); // Wait for the next bit period
        // ad9833_set_frequency_hz(2200.0F);
        // sleep_ms(1000 / BAUD_RATE); // Wait for the next bit period
    }

failed:
    return ret;
}
