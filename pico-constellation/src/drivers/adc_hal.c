#include "adc_hal.h"

#include <stdlib.h>
#include <string.h>
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include "c-logger.h"

#define ADC_PIN 26
#define ADC_CHANNEL 0

static adc_buffer_ready_callback_t user_callback = NULL;

static int sample_rate = 0;
static int buffer_chunk_size = 0;

static uint16_t *circular_buffer = NULL;
static size_t buffer_capacity = 0;
static size_t write_index = 0;
static size_t read_index = 0;

static int dma_chan = -1;
static bool is_running = false;

int adc_hal_init(void)
{
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(ADC_CHANNEL);

    dma_chan = dma_claim_unused_channel(true);
    if (dma_chan < 0)
    {
        LOG_ERROR("No DMA channel available");
        return -1;
    }

    LOG_INFO("ADC HAL initialized");
    return 0;
}

int adc_hal_deinit(void)
{
    if (is_running)
        adc_hal_stop();

    if (dma_chan >= 0)
    {
        dma_channel_unclaim(dma_chan);
        dma_chan = -1;
    }

    free(circular_buffer);
    circular_buffer = NULL;

    LOG_INFO("ADC HAL deinitialized");
    return 0;
}

int adc_hal_set_sample_rate(int rate)
{
    if (is_running)
    {
        LOG_WARN("Cannot change sample rate while running");
        return -1;
    }
    if (rate <= 0)
        return -1;
    sample_rate = rate;
    return 0;
}

int adc_hal_set_sample_size(int sample_size)
{
    if (is_running)
    {
        LOG_WARN("Cannot change buffer size while running");
        return -1;
    }
    if (sample_size <= 0)
    {
        LOG_ERROR("Invalid sample size: %d", sample_size);
        return -1;
    }

    if (circular_buffer)
    {
        free(circular_buffer);
        circular_buffer = NULL;
    }

    buffer_capacity = sample_size * 8; // Allocate double the size for circular buffer
    circular_buffer = calloc(buffer_capacity, sizeof(uint16_t));
    if (!circular_buffer)
    {
        LOG_ERROR("Failed to allocate circular buffer of size %zu", buffer_capacity * sizeof(uint16_t));
        return -1;
    }

    buffer_chunk_size = sample_size;
    write_index = 0;
    read_index = 0;
    return 0;
}

int adc_hal_set_callback(adc_buffer_ready_callback_t cb)
{
    user_callback = cb;
    return 0;
}

static void __isr dma_handler(void)
{
    dma_hw->ints0 = 1u << dma_chan;

    // Calculate free space in buffer (one slot less than full)
    size_t free_space = (read_index + buffer_capacity - write_index - 1) % buffer_capacity;

    if (free_space < buffer_chunk_size)
    {
        // Not enough space: overwrite old data by advancing read_index
        size_t samples_to_free = buffer_chunk_size - free_space;
        read_index = (read_index + samples_to_free) % buffer_capacity;
    }

    // Now advance write_index by chunk size as DMA just wrote this many samples
    write_index = (write_index + buffer_chunk_size) % buffer_capacity;

    if (user_callback)
    {
        size_t available = (write_index + buffer_capacity - read_index) % buffer_capacity;
        user_callback(available);
    }

    dma_channel_set_read_addr(dma_chan, &adc_hw->fifo, false);
    dma_channel_set_write_addr(dma_chan, &circular_buffer[write_index], false);
    dma_channel_set_trans_count(dma_chan, buffer_chunk_size, true);
}

int adc_hal_start(void)
{
    if (is_running)
        return 0;

    float clkdiv = 48000000.0f / sample_rate;
    adc_set_clkdiv(clkdiv);

    adc_fifo_setup(true, true, 1, false, false);

    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_dreq(&c, DREQ_ADC);

    dma_channel_configure(
        dma_chan, &c,
        &circular_buffer[write_index],
        &adc_hw->fifo,
        buffer_chunk_size,
        false);

    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    dma_channel_start(dma_chan);
    adc_run(true);

    is_running = true;
    LOG_INFO("ADC sampling started (%d Hz, %d samples)", sample_rate, buffer_chunk_size);
    return 0;
}

int adc_hal_stop(void)
{
    if (!is_running)
        return 0;

    dma_channel_abort(dma_chan);
    adc_run(false);

    irq_set_enabled(DMA_IRQ_0, false);
    dma_channel_set_irq0_enabled(dma_chan, false);

    is_running = false;
    LOG_INFO("ADC sampling stopped");
    return 0;
}

int adc_samples_available(int *num_samples)
{
    if (!num_samples)
        return -1;
    *num_samples = (write_index + buffer_capacity - read_index) % buffer_capacity;
    return 0;
}

int adc_hal_get_samples(uint16_t *buffer, size_t max_size, int *num_samples)
{
    if (!buffer || !num_samples)
        return -1;

    int available;
    adc_samples_available(&available);
    if (available <= 0)
    {
        *num_samples = 0;
        return 0;
    }

    int to_copy = available < (int)max_size ? available : (int)max_size;

    for (int i = 0; i < to_copy; ++i)
    {
        buffer[i] = circular_buffer[(read_index + i) % buffer_capacity];
    }

    read_index = (read_index + to_copy) % buffer_capacity;
    *num_samples = to_copy;
    return 0;
}