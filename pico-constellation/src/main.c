#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>
#include <time.h>
#include "c-logger.h"
#include "ad9833.h"

#include "fsk_decoder.h"
#include "adc_hal.h"

#define BAUD_RATE 8 // Baud rate for FSK modulation

void bit_callback(int bit)
{
    if (bit == 0)
    {
        LOG_INFO("Received bit: 0");
        ad9833_set_frequency_hz(1100.0F); // Set frequency to 1100 Hz for bit 0
    }
    else if (bit == 1)
    {
        LOG_INFO("Received bit: 1");
        ad9833_set_frequency_hz(2200.0F); // Set frequency to 2200 Hz for bit 1
    }
    else
    {
        LOG_ERROR("Invalid bit received: %d", bit);
    }
}

int main()
{
    int ret = 0;
    stdio_init_all();
    sleep_ms(2000); // Wait for USB to be ready
    log_init(LOG_LEVEL_INFO);
    LOG_INFO("Booting Pico Constellation...");

    if (ad9833_init())
    {
        LOG_FATAL("Failed to initialize AD9833");
        ret = -1;
        goto failed;
    }

    fsk_decoder_handle_t fsk_decoder;
    fsk_decoder.baud_rate = BAUD_RATE;
    fsk_decoder.sample_rate = 2400; // Sample rate of 2400 Hz
    fsk_decoder.power_threshold = 100000.0F; // Power threshold for detecting
    fsk_decoder.bit_callback = bit_callback;
    fsk_decoder.adc_init = adc_hal_init;
    fsk_decoder.adc_start = adc_hal_start;
    fsk_decoder.adc_stop = adc_hal_stop;
    fsk_decoder.adc_set_callback = adc_hal_set_callback;
    fsk_decoder.adc_set_sample_rate = adc_hal_set_sample_rate;
    fsk_decoder.adc_set_sample_size = adc_hal_set_sample_size;
    fsk_decoder.adc_get_samples = adc_hal_get_samples;

    if (fsk_decoder_init(&fsk_decoder))
    {
        LOG_FATAL("Failed to initialize FSK decoder");
        ret = -1;
        goto failed;
    }

    ad9833_set_mode(AD9833_MODE_SINE);

    ad9833_set_frequency_hz(2200.0F); // Set initial frequency to 2200 Hz

    uint64_t start_time = to_ms_since_boot(get_absolute_time());
    uint64_t current_time = start_time;
    uint64_t switch_time = (1000 / BAUD_RATE); // Switch frequency every bit duration

    bool switch_frequency = true;

    if (fsk_decoder_start(&fsk_decoder))
    {
        LOG_FATAL("Failed to start FSK decoder");
        ret = -1;
        goto failed;
    }

    while (true)
    {
        current_time = to_ms_since_boot(get_absolute_time());
        if (current_time - start_time >= switch_time)
        {
            if (switch_frequency)
            {
                ad9833_set_frequency_hz(1100.0F); // Switch to 1100 Hz
            }
            else
            {
                ad9833_set_frequency_hz(2200.0F); // Switch to 2200 Hz
            }
            switch_frequency = !switch_frequency; // Toggle frequency for next iteration
            start_time = current_time;            // Reset start time
        }

        if (fsk_decoder_process(&fsk_decoder))
        {
            LOG_ERROR("Failed to process FSK decoder");
            ret = -1;
            goto failed;
        }
    }

failed:
    return ret;
}
