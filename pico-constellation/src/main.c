#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>
#include "hardware/clocks.h"
#include "ad9833.h"
#include "adc_fft.h"
#include "c-logger.h"

int main()
{
    // Set system clock to 270MHz
    //set_sys_clock_khz(270000, true);
    stdio_init_all();
    log_init(LOG_LEVEL_DEBUG);
    sleep_ms(2000);
    LOG_INFO("Booting Pico Constellation...");

    ad9833_init();
    adc_fft_init();

    ad9833_set_mode(AD9833_MODE_SINE);

    const float min_frequency = 10000;
    const float max_frequency = 100000;
    const float frequency_step = 10000;
    float frequency;
    while (1)
    {
        frequency = 100;
        printf("Setting Frequency:  %.2f Hz\n", frequency);
        ad9833_set_frequency_hz(frequency);
        adc_fft_start_sampling();
        adc_fft_perform_fft();
        //sleep_ms(500);

        frequency = 2200;
        printf("Setting Frequency:  %.2f Hz\n", frequency);
        ad9833_set_frequency_hz(frequency);
        adc_fft_start_sampling();
        adc_fft_perform_fft();
        //sleep_ms(500);
        continue;

        for (float frequency = min_frequency; frequency < max_frequency; frequency += frequency_step)
        {
            printf("Setting Frequency:  %.2f Hz\n", frequency);
            ad9833_set_frequency_hz(frequency);
            adc_fft_start_sampling();
            adc_fft_perform_fft();
            // sleep_ms(100);
        }
        for (float frequency = max_frequency; frequency > 100; frequency -= frequency_step)
        {
            printf("Setting Frequency:  %.2f Hz\n", frequency);
            ad9833_set_frequency_hz(frequency);
            adc_fft_start_sampling();
            adc_fft_perform_fft();
            // sleep_ms(100);
        }
    }

    return 0;
}
