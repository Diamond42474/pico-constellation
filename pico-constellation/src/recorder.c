/**
 * @file recorder.c
 * @author Diamond42474
 *
 * @brief Records ADC samples and streams them to the host over USB CDC.
 *
 * When pconfig_DEBUG_RECORDING_ENABLED is set, the FSK decoder calls
 * debug_handle_recording() once for every raw ADC sample it processes.
 * This module buffers those samples in a ring and the main loop drains
 * the ring into fixed-size frames that are written to the USB serial
 * console (the Pico appears as a CDC-ACM device, e.g. /dev/ttyACM0 or
 * COMx on Windows).
 *
 * Frame format (all fields little-endian):
 *
 *   magic   uint16  RECORDER_MAGIC
 *   seq     uint16  frame sequence number (wraps at 16 bits)
 *   count   uint16  number of samples in this frame
 *   samples uint16[count]  raw 12-bit ADC samples
 *   crc     uint16  CRC-16/CCITT-FALSE over all preceding bytes
 *
 * Robustness: the host can resynchronize on the magic word after any
 * byte loss, verify frames with the CRC, and detect dropped frames
 * through gaps in the sequence number. If the host is not keeping up,
 * the ring drops samples (never blocks the decoder) and the sequence
 * gaps make that visible.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "c-logger.h"
#include "peregrine-constellation.h"
#include "debug.h"

#if pconfig_DEBUG_RECORDING_ENABLED

#define RECORDER_MAGIC (0x5AA5)         // Sync word marking the start of a frame
#define RECORDER_FRAME_SAMPLES (512)    // Samples per frame
#define RECORDER_RING_SAMPLES (2048)    // Samples held before dropping (~46 ms at 44.1 kS/s)

#define RECORDER_HEADER_SIZE (6)        // magic + seq + count
#define RECORDER_FOOTER_SIZE (2)        // crc
#define RECORDER_FRAME_SIZE (RECORDER_HEADER_SIZE + (RECORDER_FRAME_SAMPLES * sizeof(uint16_t)) + RECORDER_FOOTER_SIZE)

// Ring buffer of raw samples
static uint16_t sample_ring[RECORDER_RING_SAMPLES];
static size_t ring_head = 0;            // Next write index (decoder)
static size_t ring_tail = 0;            // Next read index (flusher)
static uint16_t frame_seq = 0;          // Frame sequence number, wraps at 16 bits
static uint32_t samples_dropped = 0;    // Samples dropped because the ring was full

static uint8_t frame_buffer[RECORDER_FRAME_SIZE];

/**
 * @brief Handles debug recording of ADC samples.
 *
 * Called by the FSK decoder for every raw sample. Non-blocking: if the
 * ring is full the sample is dropped and counted.
 *
 * @param sample The raw uint16_t ADC sample.
 *
 * @return error code: 0 = success, -1 = failure
 */
int debug_handle_recording(uint16_t sample)
{
    size_t next_head = (ring_head + 1) % RECORDER_RING_SAMPLES;

    if (next_head == ring_tail)
    {
        // Ring full: host is not keeping up. Drop the oldest sample so the
        // newest are always kept (the sequence gap makes the loss visible).
        ring_tail = (ring_tail + 1) % RECORDER_RING_SAMPLES;
        samples_dropped++;
    }

    sample_ring[ring_head] = sample;
    ring_head = next_head;
    return 0;
}

/**
 * @brief Computes CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over a byte buffer.
 *
 * @param data Pointer to the bytes to checksum.
 * @param len Number of bytes.
 *
 * @return The 16-bit CRC.
 */
static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++)
    {
        crc ^= (uint16_t)data[i] << 8;
        for (int bit = 0; bit < 8; bit++)
        {
            if (crc & 0x8000)
            {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            }
            else
            {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static void write_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0x00FF);
    dst[1] = (uint8_t)(value >> 8);
}

/**
 * @brief Drains the sample ring and writes complete frames to USB CDC.
 *
 * Must be called from the main loop after pc_task(). If the host is not
 * reading, the USB write may time out and drop bytes; the host recovers
 * by resynchronizing on the magic word.
 */
void recorder_flush(void)
{
    while (ring_head != ring_tail)
    {
        // Number of samples currently in the ring
        size_t available = (ring_head + RECORDER_RING_SAMPLES - ring_tail) % RECORDER_RING_SAMPLES;
        size_t count = (available < RECORDER_FRAME_SAMPLES) ? available : RECORDER_FRAME_SAMPLES;

        // Build the frame header
        write_le16(&frame_buffer[0], RECORDER_MAGIC);
        write_le16(&frame_buffer[2], frame_seq);
        write_le16(&frame_buffer[4], (uint16_t)count);

        // Fill in the samples
        for (size_t i = 0; i < count; i++)
        {
            uint16_t sample = sample_ring[ring_tail];
            ring_tail = (ring_tail + 1) % RECORDER_RING_SAMPLES;
            write_le16(&frame_buffer[RECORDER_HEADER_SIZE + (2 * i)], sample);
        }

        // Append the CRC and transmit the whole frame in one write.
        // cr_translation=false: the payload is raw binary and must never be
        // touched by CR/LF translation; newline=false.
        uint16_t crc = crc16_ccitt(frame_buffer, RECORDER_HEADER_SIZE + (count * sizeof(uint16_t)));
        write_le16(&frame_buffer[RECORDER_HEADER_SIZE + (count * sizeof(uint16_t))], crc);

        (void)stdio_put_string((const char *)frame_buffer,
                               RECORDER_HEADER_SIZE + (count * sizeof(uint16_t)) + RECORDER_FOOTER_SIZE,
                               false, false);
    }

    if (samples_dropped > 0)
    {
        LOG_WARN("%lu samples dropped (ring full)", (unsigned long)samples_dropped);
        samples_dropped = 0;
    }
}

#endif // pconfig_DEBUG_RECORDING_ENABLED

void data_callback(const uint8_t *data, size_t len, uint8_t src_addr)
{
    (void)data;
    (void)len;
    (void)src_addr;
    // Ignore data, we are only recording samples for debugging
}

int main(void)
{
    int ret = 0;

    stdio_init_all();

    log_init(LOG_LEVEL_FATAL); // Logging output will interfere with recording, so set to fatal to suppress output

    sleep_ms(2000); // Wait for USB to initialize

    LOG_INFO("Booting Pico Constellation Recorder...");

    pc_handle_t *handle = pc_init(data_callback);

    if (!handle)
    {
        LOG_FATAL("Failed to initialize Pico Constellation");
        ret = -1;
        goto failed;
    }

    // Marker line so a host can tell the recorder is ready. The frame
    // parser on the host side ignores anything before the first magic word.
    printf("PICO-CONSTELLATION RECORDER READY\n");

    while (true)
    {
        pc_task(handle);
#if pconfig_DEBUG_RECORDING_ENABLED
        recorder_flush();
#endif
    }

failed:
    return ret;
}