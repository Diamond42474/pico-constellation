#include "adc_bsp.h"

#include "adc_hal.h"
#include "c-logger.h"

#define BUFFER_COUNT (1024 * 3)

static bool data_available = false;
static bool data_overflow = false;
static uint16_t tmp_buffer[BUFFER_COUNT];

static void sample_callback(size_t size)
{
    if(data_available)
    {
        data_overflow = true;
    }
    data_available = true;
}

int adc_bsp_init(int sample_rate)
{
    adc_hal_init();
    adc_hal_set_sample_rate((int)sample_rate);
    adc_hal_set_sample_size(1024);
    adc_hal_set_callback(sample_callback);
    adc_hal_start();
    return 0;
}

int adc_bsp_task()
{
    if (data_overflow)
    {
        data_overflow = false;
        LOG_WARN("ADC data overflow: samples were not read before next callback");
    }
    return 0;
}

bool adc_bsp_data_available()
{
    return true;
}

int adc_bsp_get_data(circular_buffer_t *buffer)
{
    if (!data_available)
    {
        return 0;
    }
    data_available = false;

    size_t samples_fetched = 0;
    adc_hal_get_samples(tmp_buffer, BUFFER_COUNT, &samples_fetched);

    for (size_t i = 0; i < samples_fetched; i++)
    {
        if(circular_buffer_push(buffer, &tmp_buffer[i]))
        {
            LOG_ERROR("Failed to push sample to circular buffer");
            return -1;
        }
    }

    return 0;
}