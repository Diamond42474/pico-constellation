#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>
#include "ad9833.h"

int main()
{
    stdio_init_all();
    sleep_ms(2000);
    printf("Starting...\n");

    ad9833_init();

    ad9833_set_mode(AD9833_MODE_SINE);

    float frequency = 100;
    const float max_frequency = 1000;
    const float frequency_step = 10;
    while (1)
    {
        for (frequency = 100; frequency < max_frequency; frequency += frequency_step)
        {
            ad9833_set_frequency_hz(frequency);
            sleep_ms(10);
        }
        for (frequency = max_frequency; frequency > 100; frequency -= frequency_step)
        {
            ad9833_set_frequency_hz(frequency);
            sleep_ms(10);
        }
    }

    return 0;
}
