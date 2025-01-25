#include "ad9833.h"

#include "fhdm-ad9833-pico.h"
#include "pico/stdlib.h"
#include <stdio.h>

static bool initialized = false;
static struct fhdm_ad9833 ad9833;

int ad9833_init(void)
{
    int ret = 0;

    if (initialized)
    {
        return 0;
    }

    // Initialize the AD9833 driver
    fhdm_ad9833_pico_new(&ad9833);

    initialized = true;

failed:
    return ret;
}

int ad9833_deinit(void)
{
    int ret = 0;

failed:
    return ret;
}

int ad9833_set_mode(ad9833_mode_t mode)
{
    int ret = 0;

        if (!initialized)
    {
        if (ad9833_init())
        {
            ret = -1;
            goto failed;
        }
    }

    switch (mode)
    {
    case AD9833_MODE_SINE:
        ad9833.set_mode(&ad9833, FHDM_AD9833_MODE_SINE);
        break;
    case AD9833_MODE_TRIANGLE:
        ad9833.set_mode(&ad9833, FHDM_AD9833_MODE_TRIANGLE);
        break;
    case AD9833_MODE_SQUARE:
        ad9833.set_mode(&ad9833, FHDM_AD9833_MODE_SQUARE);
        break;
    case AD9833_MODE_SLEEP:
        ad9833.set_mode(&ad9833, FHDM_AD9833_MODE_SLEEP);
        break;
    default:
        ret = -1;
        goto failed;
    }

failed:
    return ret;
}

int ad9833_set_frequency_hz(float frequency)
{
    int ret = 0;

    if (!initialized)
    {
        if (ad9833_init())
        {
            ret = -1;
            goto failed;
        }
    }

    ad9833.set_frequency(&ad9833, frequency);

failed:
    return ret;
}