/**
 * @file recorder.c
 * @author Diamond42474
 *
 * @brief Records ADC samples, filtered outputs, and metrics for debugging purposes.
 */

#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "c-logger.h"
#include "peregrine-constellation.h"
#include "debug.h"

#if pconfig_DEBUG_RECORDING_ENABLED

#define DEBUG_BUFFER_SIZE 4096

typedef struct __attribute__((packed))
{
    uint16_t sample;
    uint8_t filtered_1200;
    uint8_t filtered_2200;
    uint8_t metric;
} debug_record_t;

static debug_record_t debug_buffer[DEBUG_BUFFER_SIZE];
static size_t debug_buffer_index = 0;

/**
 * @brief Convert a normalized amplitude value (0.0 to 1.0) to 8-bit.
 */
static uint8_t amplitude_to_u8(float value)
{
    if (value <= 0.0f)
        return 0;

    if (value >= 1.0f)
        return 255;

    return (uint8_t)(value * 255.0f);
}

/**
 * @brief Record one set of debug data.
 *
 * Data is buffered and transmitted as binary records over USB.
 *
 * Record format:
 *   uint16_t sample
 *   uint8_t  filtered_1200
 *   uint8_t  filtered_2200
 *   uint8_t  metric
 *
 * Total: 5 bytes per record.
 */
int debug_handle_recording(
    uint16_t sample,
    float filtered_1200,
    float filtered_2200,
    float metric)
{
    debug_buffer[debug_buffer_index].sample = sample;
    debug_buffer[debug_buffer_index].filtered_1200 =
        amplitude_to_u8(filtered_1200);
    debug_buffer[debug_buffer_index].filtered_2200 =
        amplitude_to_u8(filtered_2200);
    debug_buffer[debug_buffer_index].metric =
        amplitude_to_u8(metric);

    debug_buffer_index++;

    if (debug_buffer_index >= DEBUG_BUFFER_SIZE)
    {
        fwrite(
            debug_buffer,
            sizeof(debug_record_t),
            DEBUG_BUFFER_SIZE,
            stdout);

        fflush(stdout);

        debug_buffer_index = 0;
    }

    return 0;
}

#endif // pconfig_DEBUG_RECORDING_ENABLED

void data_callback(const uint8_t *data, size_t len, uint8_t src_addr)
{
    return; // Ignore data, we are only recording samples for debugging
}

int main(void)
{
    int ret = 0;

    stdio_init_all();

    sleep_ms(2000); // Wait for USB to initialize

    log_init(LOG_LEVEL_ERROR); // Logging output will interfere with recording, so set to error to suppress output

    LOG_INFO("Booting Pico Constellation Recorder...");

    pc_handle_t *handle = pc_init(data_callback);

    if (!handle)
    {
        LOG_FATAL("Failed to initialize Pico Constellation");
        ret = -1;
        goto failed;
    }

    while (true)
    {
        pc_task(handle);
    }

failed:
    return ret;
}