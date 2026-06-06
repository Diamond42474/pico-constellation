#include "dac_bsp.h"
#include "ad9833.h"

int dac_bsp_init()
{
    ad9833_init();
    ad9833_set_mode(AD9833_MODE_SINE);
    return 0;
}

int dac_bsp_task()
{
    return 0;
}

int dac_bsp_set_tone(float frequency)
{
    ad9833_set_frequency_hz(frequency);
    return 0;
}