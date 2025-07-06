#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>
#include <time.h>
#include "c-logger.h"
#include "communication_interface.h"

#include "ad9833.h"
#include "fsk_decoder.h"

#define POWER_THRESHOLD 100000.0f // Power threshold for detecting signals
#define SAMPLE_RATE 2400          // 2400 Hz sample rate
#define BAUD_RATE 8               // bits per second

int main()
{
    int ret = 0;
    stdio_init_all();
    sleep_ms(2000); // Wait for USB to be ready
    log_init(LOG_LEVEL_DEBUG);
    LOG_INFO("Booting Pico Constellation...");

    if (ad9833_init())
    {
        LOG_FATAL("Failed to initialize AD9833");
        ret = -1;
        goto failed;
    }
    if (fsk_decoder_init())
    {
        LOG_FATAL("Failed to initialize FSK decoder");
        ret = -1;
        goto failed;
    }
    if (fsk_decoder_set_baud_rate(BAUD_RATE))
    {
        LOG_FATAL("Failed to set FSK decoder baud rate");
        ret = -1;
        goto failed;
    }
    if (fsk_decoder_set_sample_rate(SAMPLE_RATE))
    {
        LOG_FATAL("Failed to set FSK decoder sample rate");
        ret = -1;
        goto failed;
    }

    if (fsk_decoder_set_frequencies(1100.0f, 2200.0f))
    {
        LOG_FATAL("Failed to set FSK decoder frequencies");
        ret = -1;
        goto failed;
    }
    if (fsk_decoder_set_power_threshold(POWER_THRESHOLD))
    {
        LOG_FATAL("Failed to set FSK decoder power threshold");
        ret = -1;
        goto failed;
    }

    ad9833_set_mode(AD9833_MODE_SINE);

    ad9833_set_frequency_hz(2200.0F); // Set initial frequency to 2200 Hz

    if (fsk_decoder_start())
    {
        LOG_FATAL("Failed to start FSK decoder");
        ret = -1;
        goto failed;
    }

    uint64_t start_time = to_ms_since_boot(get_absolute_time());
    uint64_t current_time = start_time;
    uint64_t switch_time = (1000 / BAUD_RATE); // Switch frequency every bit duration

    bool switch_frequency = true;

    while (true)
    {
        fsk_decoder_process();
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
    }

failed:
    return ret;
}
