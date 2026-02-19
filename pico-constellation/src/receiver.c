#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>
#include "c-logger.h"
#include "HAL_time.h"
#include "drivers/adc_hal.h"

#include "encoding/encoder.h"
#include "adc_hal.h"
#include "utils/fsk_utils.h"
#include "decoding/decoder.h"
#include "decoding/fsk_decoder.h"
#include "decoding/byte_assembler.h"

#define BAUD_RATE 32 // Baud rate for FSK modulation
#define F1 1100
#define F0 2200
#define POWER_THRESHOLD 100000000.0f // Power threshold for detecting bits

static bool sample_ready = false;
static size_t available_samples = 0;

void sample_callback(size_t size)
{
    sample_ready = true;
    available_samples = size;
}

int main(void)
{
    stdio_init_all();
    sleep_ms(2000); // Wait for USB to be ready
    log_init(LOG_LEVEL_INFO);
    LOG_INFO("Booting Pico Constellation...");

    double sample_rate = calculate_sample_rate(F0, F1, BAUD_RATE);
    size_t samples_per_bit = (size_t)(sample_rate / BAUD_RATE);
    LOG_INFO("FSK Timing: Sample Rate = %f Hz", sample_rate);
    LOG_INFO("FSK Timing: Samples per Bit = %d", samples_per_bit);

    adc_hal_init();
    adc_hal_set_sample_rate((int)sample_rate);
    adc_hal_set_sample_size(samples_per_bit);
    adc_hal_set_callback(sample_callback);
    adc_hal_start();

    decoder_handle_t decoder;
    fsk_decoder_handle_t fsk_decoder;
    byte_assembler_handle_t byte_assembler;

    // Initialize FSK Decoder
    fsk_decoder_init(&fsk_decoder);
    fsk_decoder_set_rates(&fsk_decoder, samples_per_bit, (int)sample_rate);
    fsk_decoder_set_frequencies(&fsk_decoder, F0, F1);
    fsk_decoder_set_power_threshold(&fsk_decoder, POWER_THRESHOLD);
    // Initialize Byte Assembler
    byte_assembler_init(&byte_assembler);
    if (byte_assembler_set_preamble(&byte_assembler, 0xABBA))
    {
        LOG_ERROR("Failed to set preamble for byte assembler");
        return -1;
    } // Buffer for 256 bytes

    // Initialize Decoder
    decoder_init(&decoder);
    decoder_set_bit_decoder(&decoder, BIT_DECODER_FSK, &fsk_decoder);
    decoder_set_byte_decoder(&decoder, BYTE_DECODER_BIT_STUFFING, &byte_assembler);
    decoder_set_input_buffer_size(&decoder, samples_per_bit * 128); // Input buffer for samples
    decoder_set_output_buffer_size(&decoder, 10);                   // Output buffer for packets

    while (true)
    {
        decoder_task(&decoder);
        if (sample_ready)
        {
            sample_ready = false;
            int sample_num = available_samples;
            uint16_t buffer[sample_num];
            int samples_fetched = 0;
            adc_hal_get_samples(buffer, sample_num, &samples_fetched);
            decoder_process_samples(&decoder, buffer, samples_fetched);
        }
    }
    return 0;
}