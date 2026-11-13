
/**
 * @file recorder.c
 * @author Diamond42474
 *
 * @brief Records ADC samples and streams them to the host over USB CDC.
 *
 * When pconfig_DEBUG_RECORDING_ENABLED is set, the FSK decoder calls
 * debug_handle_recording() once for every raw ADC sample it processes.
 *
 * Samples are stored in a circular buffer. The main loop periodically
 * drains the buffer into fixed-size frames and transmits them over USB.
 *
 * The producer and consumer execute sequentially on the same thread.
 *
 * Frame format (all fields little-endian):
 *
 *   magic                   uint16  RECORDER_MAGIC
 *   seq                     uint16  Frame sequence number
 *   first_sample_index      uint32  Index of first sample in frame
 *   count                   uint16  Number of samples (always 512)
 *   dropped_since_last      uint32  Samples dropped since previous frame
 *   samples                 uint16[count] Raw ADC samples
 *   crc                     uint16  CRC-16/CCITT-FALSE over preceding bytes
 *
 * Header size: 14 bytes
 * Maximum frame size: 1040 bytes
 *
 * Sample indices count every sample received by debug_handle_recording(),
 * including samples subsequently dropped from the ring.
 *
 * If the ring fills, the oldest samples are discarded to preserve the
 * newest samples. The sample index and dropped counter allow the host
 * to identify and quantify discontinuities.
 *
 * Frames are transmitted only when a complete frame is available.
 */

#include <stdint.h>
#include <stddef.h>

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "c-logger.h"
#include "peregrine-constellation.h"
#include "debug.h"

#if pconfig_DEBUG_RECORDING_ENABLED

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

#define RECORDER_MAGIC                 (0x5AA5)

#define RECORDER_FRAME_SAMPLES         (512)
#define RECORDER_RING_SAMPLES          (2048)

#define RECORDER_HEADER_SIZE           (14)
#define RECORDER_FOOTER_SIZE           (2)

#define RECORDER_FRAME_SIZE \
    (RECORDER_HEADER_SIZE + \
     (RECORDER_FRAME_SAMPLES * sizeof(uint16_t)) + \
     RECORDER_FOOTER_SIZE)

// -----------------------------------------------------------------------------
// Sample ring buffer
// -----------------------------------------------------------------------------

typedef struct
{
    uint16_t sample;
    uint32_t index;
} recorder_sample_t;

static recorder_sample_t sample_ring[RECORDER_RING_SAMPLES];

static size_t ring_head = 0;
static size_t ring_tail = 0;
static size_t ring_count = 0;

// Total number of samples received by the recorder.
// Wraps naturally at 32 bits.
static uint32_t next_sample_index = 0;

// Frame sequence number.
// Wraps naturally at 16 bits.
static uint16_t frame_seq = 0;

// Samples discarded since the previous transmitted frame.
// Wraps naturally at 32 bits.
static uint32_t samples_dropped = 0;

// Frame serialization buffer.
static uint8_t frame_buffer[RECORDER_FRAME_SIZE];

// -----------------------------------------------------------------------------
// Recording callback
// -----------------------------------------------------------------------------

/**
 * @brief Records one ADC sample.
 *
 * Nonblocking with respect to USB transmission.
 *
 * If the ring is full, the oldest sample is discarded to make room
 * for the newest sample.
 *
 * This function and recorder_flush() must execute sequentially on
 * the same thread.
 *
 * @param sample Raw ADC sample.
 *
 * @return 0 on success.
 */
int debug_handle_recording(uint16_t sample)
{
    uint32_t sample_index = next_sample_index++;

    // Discard the oldest sample if the ring is full.
    if (ring_count == RECORDER_RING_SAMPLES)
    {
        ring_tail = (ring_tail + 1) % RECORDER_RING_SAMPLES;
        ring_count--;

        samples_dropped++;
    }

    // Insert the new sample.
    sample_ring[ring_head].sample = sample;
    sample_ring[ring_head].index = sample_index;

    ring_head = (ring_head + 1) % RECORDER_RING_SAMPLES;
    ring_count++;

    return 0;
}

// -----------------------------------------------------------------------------
// Serialization helpers
// -----------------------------------------------------------------------------

/**
 * @brief Writes a 16-bit integer in little-endian format.
 */
static void write_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)(value >> 8);
}

/**
 * @brief Writes a 32-bit integer in little-endian format.
 */
static void write_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)(value >> 8);
    dst[2] = (uint8_t)(value >> 16);
    dst[3] = (uint8_t)(value >> 24);
}

// -----------------------------------------------------------------------------
// CRC-16/CCITT-FALSE
// -----------------------------------------------------------------------------

/**
 * @brief Computes CRC-16/CCITT-FALSE.
 *
 * Polynomial: 0x1021
 * Initial value: 0xFFFF
 * Reflected input: false
 * Reflected output: false
 * Final XOR: 0x0000
 *
 * @param data Pointer to the input buffer.
 * @param len Number of bytes.
 *
 * @return Computed 16-bit CRC.
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

// -----------------------------------------------------------------------------
// Frame construction and transmission
// -----------------------------------------------------------------------------

/**
 * @brief Builds and transmits one complete recording frame.
 *
 * A frame contains exactly RECORDER_FRAME_SAMPLES samples.
 *
 * The first sample index and dropped-sample count allow the host
 * to identify discontinuities in the recording.
 *
 * @note stdio_put_string() sends through the enabled Pico stdio
 *       backends. Configure USB CDC as the intended output backend.
 */
static void recorder_send_frame(void)
{
    size_t offset = 0;

    // -------------------------------------------------------------------------
    // Frame header
    // -------------------------------------------------------------------------

    // Magic word.
    write_le16(&frame_buffer[offset], RECORDER_MAGIC);
    offset += 2;

    // Frame sequence number.
    write_le16(&frame_buffer[offset], frame_seq++);
    offset += 2;

    // Index of the first sample in this frame.
    uint32_t first_sample_index = sample_ring[ring_tail].index;

    write_le32(&frame_buffer[offset], first_sample_index);
    offset += 4;

    // Sample count.
    write_le16(&frame_buffer[offset], RECORDER_FRAME_SAMPLES);
    offset += 2;

    // Samples dropped since the previous frame.
    write_le32(&frame_buffer[offset], samples_dropped);
    offset += 4;

    // Reset the counter for the next frame.
    samples_dropped = 0;

    // -------------------------------------------------------------------------
    // Sample payload
    // -------------------------------------------------------------------------

    for (size_t i = 0; i < RECORDER_FRAME_SAMPLES; i++)
    {
        uint16_t sample = sample_ring[ring_tail].sample;

        write_le16(&frame_buffer[offset], sample);
        offset += 2;

        ring_tail = (ring_tail + 1) % RECORDER_RING_SAMPLES;
        ring_count--;
    }

    // -------------------------------------------------------------------------
    // CRC
    // -------------------------------------------------------------------------

    uint16_t crc = crc16_ccitt(frame_buffer, offset);

    write_le16(&frame_buffer[offset], crc);
    offset += 2;

    // -------------------------------------------------------------------------
    // USB transmission
    // -------------------------------------------------------------------------

    // Binary data: no newline or CR/LF translation.
    // stdio_put_string() returns void, so individual write success
    // cannot be determined through this API.
    stdio_put_string(
        (const char *)frame_buffer,
        (int)offset,
        false,
        false
    );
}

// -----------------------------------------------------------------------------
// Main-loop recorder interface
// -----------------------------------------------------------------------------

/**
 * @brief Drains the sample ring and transmits complete frames.
 *
 * Must be called from the main loop after pc_task().
 *
 * Partial frames remain buffered until enough samples arrive.
 */
void recorder_flush(void)
{
    while (ring_count >= RECORDER_FRAME_SAMPLES)
    {
        recorder_send_frame();
    }
}

#endif // pconfig_DEBUG_RECORDING_ENABLED

// -----------------------------------------------------------------------------
// Pico Constellation callbacks
// -----------------------------------------------------------------------------

/**
 * @brief Application data callback.
 *
 * This recorder only captures raw ADC samples.
 */
void data_callback(const uint8_t *data, size_t len, uint8_t src_addr)
{
    (void)data;
    (void)len;
    (void)src_addr;
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

int main(void)
{
    int ret = 0;

    stdio_init_all();

    // Suppress normal logging because it would corrupt the binary stream.
    log_init(LOG_LEVEL_FATAL);

    // Allow USB CDC to initialize.
    sleep_ms(2000);

    LOG_INFO("Booting Pico Constellation Recorder...");

    pc_handle_t *handle = pc_init(data_callback);

    if (!handle)
    {
        LOG_FATAL("Failed to initialize Pico Constellation");
        ret = -1;
        goto failed;
    }

    // Startup marker.
    // The host parser should ignore data until it finds a valid frame.
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
