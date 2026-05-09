#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>
#include "c-logger.h"
#include "ad9833.h"
#include "HAL_time.h"

#include "encoding/encoder.h"
#include "adc_hal.h"
#include "utils/fsk_utils.h"

#define BAUD_RATE 64 // Baud rate for FSK modulation
#define F1 2200
#define F0 1200
#define POWER_THRESHOLD 1E9f // Power threshold for detecting bits
#define PTT_PIN 15           // GP15 for PTT control

static encoder_handle_t encoder = {0};
static HAL_timer_t transmitting_timer;
static HAL_timer_t PTT_delay_timer;
static HAL_timer_t intermittent_timer;
static bool transmitting = true;

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
    if (!HAL_timer_done(&transmitting_timer))
    {
        return;
    }
    HAL_timer_reset(&transmitting_timer);

    if (!encoder_data_available(&encoder))
    {
        gpio_put(PTT_PIN, 0);       // Set PTT low to stop transmission
        ad9833_set_frequency_hz(0); // Stop transmission
        HAL_timer_reset(&intermittent_timer);
        transmitting = false;
        return;
    }

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
}

int main()
{
    int ret = 0;
    stdio_init_all();
    log_init(LOG_LEVEL_FATAL);
    LOG_INFO("Booting Pico Constellation...");

    if (ad9833_init())
    {
        LOG_FATAL("Failed to initialize AD9833");
        ret = -1;
        goto failed;
    }

    double sample_rate = calculate_sample_rate(F1, F0, BAUD_RATE);
    size_t samples_per_bit = (size_t)(sample_rate / BAUD_RATE);
    LOG_INFO("FSK Timing: Sample Rate = %f Hz", sample_rate);
    LOG_INFO("FSK Timing: Samples per Bit = %d", samples_per_bit);

    encoder_init(&encoder);
    encoder_set_type(&encoder, ENCODER_TYPE_NONE);
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
    gpio_put(PTT_PIN, 0);

    char test_data[] = "  Hello, this is Pico Constellation!";
    test_data[0] = 0xAB; // Preamble byte 1
    test_data[1] = 0xBA; // Preamble byte 2
    encoder_write(&encoder, (unsigned char *)test_data, sizeof(test_data)-1);
    encoder_flush(&encoder);
    while (encoder_busy(&encoder))
    {
        encoder_task(&encoder);
    }

    ad9833_set_mode(AD9833_MODE_SINE);

    HAL_timer_start(&transmitting_timer, (1000 / BAUD_RATE) * ONE_MILLISECOND); // Start timer for bit duration
    HAL_timer_start(&intermittent_timer, 1 * ONE_SECOND);                       // Start timer for intermittent sending
    HAL_timer_start(&PTT_delay_timer, 1000 * ONE_MILLISECOND);                   // Start timer for PTT delay                                  // Set PTT high to start transmission
    gpio_put(PTT_PIN, 1);
    transmitting = true;
    while (true)
    {
        encoder_task(&encoder);

        if (transmitting)
        {
            if (HAL_timer_done(&PTT_delay_timer))
            {
                send_handler();
            }
        }
        else
        {
            if (HAL_timer_done(&intermittent_timer))
            {
                encoder_write(&encoder, (unsigned char *)test_data, sizeof(test_data)-1);
                encoder_flush(&encoder);
                HAL_timer_reset(&PTT_delay_timer);
                gpio_put(PTT_PIN, 1);
                transmitting = true;
            }
        }
    }

failed:
    return ret;
}

// 02 48 00