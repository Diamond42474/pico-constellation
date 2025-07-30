#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>
#include "c-logger.h"
#include "ad9833.h"
#include "HAL_time.h"

#include "fsk_decoder.h"
#include "byte_assembler.h"
#include "cobs_decoder.h"
#include "cobs_encoder.h"

#include "adc_hal.h"

#define BAUD_RATE 8 // Baud rate for FSK modulation
#define F1 2200
#define F0 1100
#define SAMPLE_RATE 2400 // Sample rate for ADC
#define POWER_THRESHOLD 1000000.0f // Power threshold for detecting bits

uint8_t encoded_data[256];
size_t encoded_length = 0;

void data_callback(const uint8_t *data, size_t len)
{
    printf("Decoded Data: ");
    for (size_t i = 0; i < len; i++)
    {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

HAL_timer_t transmitting_timer;
int bit_index = 0;
bool transmitting = false;
void send_handler(void)
{
    if (!transmitting)
    {
        //ad9833_set_frequency_hz(0); // Stop output if not transmitting
        return;
    }

    if (HAL_timer_done(&transmitting_timer))
    {
        //LOG_INFO("Time over: %llu ms", HAL_get_current_time_ms() - (transmitting_timer.start_time / 1000));
        HAL_timer_reset(&transmitting_timer);
        int total_bits = encoded_length * 8;
        if (bit_index >= total_bits)
        {
            ad9833_set_frequency_hz(0); // Stop output
            transmitting = false;
            LOG_INFO("Transmission complete");
            return;
        }

        int byte_index = bit_index / 8;
        int bit_in_byte = 7 - (bit_index % 8); // MSB first
        uint8_t current_byte = encoded_data[byte_index];
        bool current_bit = (current_byte >> bit_in_byte) & 0x01;

        // LOG_INFO("Sending bit %d: %d (Byte: %02X, Bit: %d)", bit_index, current_bit, current_byte, bit_in_byte);

        ad9833_set_frequency_hz(current_bit ? F1 : F0);
        bit_index++;
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

    fsk_decoder_handle_t fsk_decoder;
    fsk_decoder.baud_rate = BAUD_RATE;
    fsk_decoder.sample_rate = SAMPLE_RATE;           // Sample rate of 2400 Hz
    fsk_decoder.power_threshold = POWER_THRESHOLD; // Power threshold for detecting
    fsk_decoder.bit_callback = byte_assembler_bit_callback;
    fsk_decoder.adc_init = adc_hal_init;
    fsk_decoder.adc_start = adc_hal_start;
    fsk_decoder.adc_stop = adc_hal_stop;
    fsk_decoder.adc_set_callback = adc_hal_set_callback;
    fsk_decoder.adc_set_sample_rate = adc_hal_set_sample_rate;
    fsk_decoder.adc_set_sample_size = adc_hal_set_sample_size;
    fsk_decoder.adc_get_samples = adc_hal_get_samples;

    if (fsk_decoder_init(&fsk_decoder))
    {
        LOG_FATAL("Failed to initialize FSK decoder");
        ret = -1;
        goto failed;
    }

    fsk_decoder_set_baud_rate(&fsk_decoder, BAUD_RATE);
    fsk_decoder_set_sample_rate(&fsk_decoder, SAMPLE_RATE);
    fsk_decoder_set_frequencies(&fsk_decoder, F0, F1);
    fsk_decoder_set_power_threshold(&fsk_decoder, POWER_THRESHOLD);

    if (byte_assembler_init())
    {
        LOG_ERROR("Failed to initialize byte assembler");
        ret = -1;
        goto failed;
    }

    if (byte_assembler_set_byte_callback(cobs_decoder_input))
    {
        LOG_ERROR("Failed to set byte assembler callback");
        ret = -1;
        goto failed;
    }

    if (cobs_decoder_init())
    {
        LOG_ERROR("Failed to initialize COBS decoder");
        ret = -1;
        goto failed;
    }

    if (cobs_decoder_set_data_callback(data_callback))
    {
        LOG_ERROR("Failed to set COBS decoder data callback");
        ret = -1;
        goto failed;
    }

    char test_data[] = "H";
    size_t test_data_len = sizeof(test_data) - 1; // Exclude null terminator
    encoded_length = cobs_encode((const uint8_t *)test_data, test_data_len, encoded_data + 1, sizeof(encoded_data) - 1);
    if (encoded_length < 0)
    {
        LOG_ERROR("Failed to encode data with COBS");
        ret = -1;
        goto failed;
    }

    // Prepend preamble byte
    // encoded_data[0] = 0xFF; // Preamble
    // encoded_length += 1;
    encoded_data[0] = 0xAA; // Preamble
    encoded_length += 1;

    LOG_INFO("Encoded Data: ");
    for (size_t i = 0; i < encoded_length; i++)
    {
        printf("%02X ", encoded_data[i]);
    }
    printf("\n");
    printf("Original data: %02X\n", test_data[0]);

    ad9833_set_mode(AD9833_MODE_SINE);

    if (fsk_decoder_start(&fsk_decoder))
    {
        LOG_FATAL("Failed to start FSK decoder");
        ret = -1;
        goto failed;
    }

    transmitting = true;
    bool flag = false;

    HAL_timer_start(&transmitting_timer, (1000 / BAUD_RATE) * ONE_MILLISECOND); // Start timer for bit duration
    HAL_timer_t intermittent_timer;
    while (true)
    {

        send_handler();
        if (fsk_decoder_process(&fsk_decoder))
        {
            LOG_ERROR("Failed to process FSK decoder");
            ret = -1;
            goto failed;
        }

        if (!transmitting)
        {
            if (!flag)
            {
                LOG_INFO("Transmission complete, waiting for 3 seconds...");
                HAL_timer_start(&intermittent_timer, ONE_SECOND * 3); // Start intermittent timer
                flag = true;
            }

            // Non-blocking delay for 3000 ms
            if (HAL_timer_done(&intermittent_timer))
            {
                LOG_INFO("Resuming transmission...");
                //byte_assembler_reset();
                //cobs_decoder_reset();
                bit_index = 0; // Reset bit index for new transmission
                transmitting = true;
                flag = false;
            }
        }
    }

failed:
    return ret;
}

// 02 48 00