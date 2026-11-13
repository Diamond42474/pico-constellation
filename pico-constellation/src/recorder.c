/**
 * @file recorder.c
 * @author Diamond42474
 *
 * @brief Records ADC samples, filtered outputs, and metrics for debugging purposes.
 */

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include "pico/stdlib.h"
#include "c-logger.h"
#include "peregrine-constellation.h"
#include "debug.h"

#if pconfig_DEBUG_RECORDING_ENABLED

#define SYNC_WORD (0xABCD) // Sync word for identifying the start of a record

typedef struct __attribute__((packed))
{
    uint32_t sync;
    uint16_t sample;
} debug_record_t;

int debug_handle_recording(
    uint16_t sample)
{
    // TODO: Implement recording logic here.

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