/**
 * @file recorder.c
 * @author Diamond42474
 *
 * @brief Records raw ADC samples for debugging purposes.
 */

#include <stdio.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "c-logger.h"
#include "peregrine-constellation.h"
#include "debug.h"

#if pconfig_DEBUG_RECORDING_ENABLED

#define DEBUG_BUFFER_SIZE 4096
#define SYNC_WORD (0xABCD)  // Sync word for identifying the start of a record

typedef struct __attribute__((packed))
{
    uint16_t sync;
    uint16_t sample;
} debug_record_t;

static debug_record_t debug_buffer[DEBUG_BUFFER_SIZE];
static size_t debug_buffer_index = 0;

/**
 * @brief Record one raw ADC sample.
 *
 * Data is buffered and transmitted as binary records over USB.
 *
 * Record format:
 *   uint16_t sync
 *   uint16_t sample
 *
 * Total: 4 bytes per record.
 */
int debug_handle_recording(uint16_t sample)
{
    debug_buffer[debug_buffer_index].sync = SYNC_WORD;
    debug_buffer[debug_buffer_index].sample = sample;

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

    log_init(LOG_LEVEL_FATAL); // Logging output will interfere with recording, so set to error to suppress output

    sleep_ms(2000); // Wait for USB to initialize

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
