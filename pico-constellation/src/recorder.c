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
#include "utils/circular_buffer.h"

#if pconfig_DEBUG_RECORDING_ENABLED

#define FRAME_MAGIC (0xA5C3F17Eu)
#define FRAME_SAMPLE_COUNT (1024u)
#define CB_SIZE (8u)

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t sequence;
    uint16_t sample_count;
    uint16_t samples[FRAME_SAMPLE_COUNT];
    uint32_t crc32;
} debug_frame_t;

static debug_frame_t debug_frame;
static size_t sample_index = 0;
static uint16_t frame_sequence = 0;
static debug_frame_t frame_buffer[CB_SIZE];
static circular_buffer_t frame_cb;

/**
 * @brief Calculate an IEEE CRC-32 for frame validation on the host.
 */
static uint32_t calculate_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFu;

    while (length-- > 0)
    {
        crc ^= *data++;

        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 1u)
                crc = (crc >> 1) ^ 0xEDB88320u;
            else
                crc >>= 1;
        }
    }

    return ~crc;
}

/**
 * @brief Record one raw ADC sample.
 *
 * Data is buffered and transmitted as binary records over USB.
 *
 * Frame format:
 *   uint32_t magic
 *   uint16_t sequence
 *   uint16_t sample_count
 *   uint16_t samples[1024]
 *   uint32_t crc32 (sequence, count, and samples)
 *
 * A frame is emitted for every 1024 samples. The 32-bit marker, sequence
 * number, and CRC let the host reliably find and validate frame boundaries.
 */
int debug_handle_recording(uint16_t sample)
{
    debug_frame.samples[sample_index++] = sample;

    if (sample_index >= FRAME_SAMPLE_COUNT)
    {
        debug_frame.magic = FRAME_MAGIC;
        debug_frame.sequence = frame_sequence++;
        debug_frame.sample_count = FRAME_SAMPLE_COUNT;
        debug_frame.crc32 = calculate_crc32(
            (const uint8_t *)&debug_frame.sequence,
            sizeof(debug_frame.sequence) +
            sizeof(debug_frame.sample_count) +
            sizeof(debug_frame.samples));

        // fwrite(
        //     &debug_frame,
        //     sizeof(debug_frame),
        //     1,
        //     stdout);

        // fflush(stdout);

        circular_buffer_push(&frame_cb, &debug_frame);

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

    circular_buffer_static_init(&frame_cb, frame_buffer, sizeof(debug_frame_t), CB_SIZE);

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
        if (!circular_buffer_is_empty(&frame_cb))
        {
            // Check if fwrite is ready to send data
            if (stdio_usb_connected())
            {
                debug_frame_t frame;
                circular_buffer_pop(&frame_cb, &frame);
                fwrite(&frame, sizeof(frame), 1, stdout);
                fflush(stdout);
            }
        }
    }

failed:
    return ret;
}
