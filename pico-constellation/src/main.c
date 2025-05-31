#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>
#include <time.h>
#include "ad9833.h"
#include "adc_fft.h"
#include "c-logger.h"

int main()
{
    int ret = 0;
    stdio_init_all();
    sleep_ms(2000); // Wait for USB to be ready
    log_init(LOG_LEVEL_INFO);
    LOG_INFO("Booting Pico Constellation...");

    ad9833_init();
    adc_fft_init();

    ad9833_set_mode(AD9833_MODE_SINE);
    uint32_t loop_count = 0;
    time_t start_time = time(NULL);

    while (1) {
        ad9833_set_frequency_hz(1000);
        adc_fft_start_sampling();
        adc_fft_perform_fft();

        ad9833_set_frequency_hz(2000);
        adc_fft_start_sampling();
        adc_fft_perform_fft();

        loop_count++;

        time_t current_time = time(NULL);
        if (current_time - start_time >= 1) {
            printf("Loops per second: %u\n", loop_count);
            loop_count = 0;
            start_time = current_time;
        }
    }

failed:
    return ret;
}
