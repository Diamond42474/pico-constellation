#include "ad9833.h"

#include <stdio.h>
#include "fhdm-ad9833-pico.h"
#include "pico/stdlib.h"
#include "c-logger.h"

static bool initialized = false;
static struct fhdm_ad9833 ad9833;

int ad9833_init(void)
{
    int ret = 0;
    LOG_INFO("Initializing AD9833");

    if (initialized)
    {
        return 0;
    }

    // Initialize the AD9833 driver
    fhdm_ad9833_pico_new(&ad9833);

    ad9833.start(&ad9833);

    initialized = true;

failed:
    return ret;
}

int ad9833_deinit(void)
{
    int ret = 0;
    LOG_INFO("Deinitializing AD9833");

failed:
    return ret;
}

int ad9833_set_mode(ad9833_mode_t mode)
{
    int ret = 0;
    LOG_DEBUG("Setting AD9833 mode to %d", mode);

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
        LOG_INFO("Setting AD9833 mode to SINE");
        ad9833.set_mode(&ad9833, FHDM_AD9833_MODE_SINE);
        break;
    case AD9833_MODE_TRIANGLE:
        LOG_INFO("Setting AD9833 mode to TRIANGLE");
        ad9833.set_mode(&ad9833, FHDM_AD9833_MODE_TRIANGLE);
        break;
    case AD9833_MODE_SQUARE:
        LOG_INFO("Setting AD9833 mode to SQUARE");
        ad9833.set_mode(&ad9833, FHDM_AD9833_MODE_SQUARE);
        break;
    case AD9833_MODE_SLEEP:
        LOG_INFO("Setting AD9833 mode to SLEEP");
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

    LOG_DEBUG("Setting AD9833 frequency to %.2f Hz", frequency);

    if (!initialized)
    {
        if (ad9833_init())
        {
            ret = -1;
            printf("Failed to initialize AD9833\n");
            goto failed;
        }
    }

    ad9833.set_frequency(&ad9833, frequency);

failed:
    return ret;
}