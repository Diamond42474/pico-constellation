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

#define FRAME_MAGIC (0xA5C3F17Eu)
#define FRAME_SAMPLE_COUNT (1024u)

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t samples[FRAME_SAMPLE_COUNT];
} debug_frame_t;

static debug_frame_t debug_frame;
static size_t sample_index = 0;

/**
 * @brief Record one raw ADC sample.
 *
 * Data is buffered and transmitted as binary records over USB.
 *
 * Frame format:
 *   uint32_t magic
 *   uint16_t samples[1024]
 *
 * A frame is emitted for every 1024 samples. USB CDC provides reliable,
 * ordered delivery, so the marker is only used to find frame boundaries when
 * the host starts reading in the middle of a frame.
 */
int debug_handle_recording(uint16_t sample)
{
    debug_frame.samples[sample_index++] = sample;

    if (sample_index >= FRAME_SAMPLE_COUNT)
    {
        debug_frame.magic = FRAME_MAGIC;

        fwrite(
            &debug_frame,
            sizeof(debug_frame),
            1,
            stdout);

        fflush(stdout);

        sample_index = 0;
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
