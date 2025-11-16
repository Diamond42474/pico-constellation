#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>
#include "c-logger.h"
#include "ad9833.h"
#include "HAL_time.h"

#include "encoder.h"
#include "adc_hal.h"
#include "fsk_utils.h"

#define BAUD_RATE 128 // Baud rate for FSK modulation
#define F1 2200
#define F0 1100
#define POWER_THRESHOLD 100000000.0f // Power threshold for detecting bits
#define PTT_PIN 15                   // GP15 for PTT control

static encoder_handle_t encoder = {0};
HAL_timer_t transmitting_timer;
HAL_timer_t PTT_delay_timer;

void data_callback(const uint8_t *data, size_t len)
{
    LOG_INFO("Decoded Data:");
    for (size_t i = 0; i < len; i++)
    {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

void send_handler(void)
{
    // gpio_put(PTT_PIN, 1);

    if (!HAL_timer_done(&transmitting_timer))
    {
        return;
    }
    HAL_timer_reset(&transmitting_timer);

    bool bit;
    encoder_read(&encoder, &bit);
    if (bit)
    {
        ad9833_set_frequency_hz(F1);
    }
    else
    {
        ad9833_set_frequency_hz(F0);
    }

    if (!encoder_data_available(&encoder))
    {
        ad9833_set_frequency_hz(0); // Stop transmission
        gpio_put(PTT_PIN, 0);
        return;
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

    fsk_timing_t fsk_timing = fsk_calculate_timing(BAUD_RATE, F1, F0);
    LOG_INFO("FSK Timing: Sample Rate = %d Hz, Samples per Bit = %d", fsk_timing.sample_rate, fsk_timing.samples_per_bit);

    encoder_init(&encoder);
    encoder_set_type(&encoder, ENCODER_TYPE_COBS);
    encoder_set_input_size(&encoder, 1024);
    encoder_set_output_size(&encoder, 1024);

    LOG_INFO("Initializing Encoder...");
    while (encoder_busy(&encoder))
    {
        encoder_task(&encoder);
    }

    // Initialize GPPTT_PIN output and set it low
    gpio_init(PTT_PIN);
    gpio_set_dir(PTT_PIN, GPIO_OUT);
    // set internal pull down resistor
    gpio_pull_down(PTT_PIN);
    gpio_put(PTT_PIN, 1);

    char test_data[] = "Hello, this is a test message from Pico Constellation!";
    encoder_write(&encoder, (unsigned char *)test_data, sizeof(test_data));
    encoder_flush(&encoder);
    while (encoder_busy(&encoder))
    {
        encoder_task(&encoder);
    }

    ad9833_set_mode(AD9833_MODE_SINE);

    HAL_timer_start(&transmitting_timer, (1000 / BAUD_RATE) * ONE_MILLISECOND); // Start timer for bit duration
    HAL_timer_t intermittent_timer;
    HAL_timer_start(&intermittent_timer, 3 * ONE_SECOND); // Start timer for intermittent sending
    HAL_timer_start(&PTT_delay_timer, 500 * ONE_MILLISECOND); // Start timer for PTT delay
    while (true)
    {
        encoder_task(&encoder);

        if (encoder_data_available(&encoder) || encoder_busy(&encoder))
        {
            if (HAL_timer_done(&PTT_delay_timer))
            {
                send_handler();
            }
            HAL_timer_reset(&intermittent_timer);
        }
        else
        {
            gpio_put(PTT_PIN, 0);
            if (HAL_timer_done(&intermittent_timer))
            {
                encoder_write(&encoder, (unsigned char *)test_data, sizeof(test_data));
                encoder_flush(&encoder);
                HAL_timer_reset(&PTT_delay_timer);
                gpio_put(PTT_PIN, 1);
            }
        }
    }

failed:
    return ret;
}

// 02 48 00