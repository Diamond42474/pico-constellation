#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>
#include <time.h>
#include "c-logger.h"
#include "communication_interface.h"

#include "ad9833.h"

#define BAUD_RATE 8 // Baud rate for FSK modulation

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

    ad9833_set_mode(AD9833_MODE_SINE);

    ad9833_set_frequency_hz(2200.0F); // Set initial frequency to 2200 Hz

    uint64_t start_time = to_ms_since_boot(get_absolute_time());
    uint64_t current_time = start_time;
    uint64_t switch_time = (1000 / BAUD_RATE); // Switch frequency every bit duration

    bool switch_frequency = true;

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
    }

failed:
    return ret;
}
