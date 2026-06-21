#include <stdio.h>
#include "pico/stdlib.h"
#include "c-logger.h"
#include "HAL_time.h"
#include "peregrine-constellation.h"
static HAL_timer_t intermittent_timer;

void data_callback(const uint8_t *data, size_t len, uint8_t src_addr)
{
    LOG_INFO("Decoded Data:");
    for (size_t i = 0; i < len; i++)
    {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

int main()
{
    int ret = 0;
    stdio_init_all();
    log_init(LOG_LEVEL_INFO);
    LOG_INFO("Booting Pico Constellation...");

    pc_handle_t *handle = pc_init(data_callback);
    if (!handle)
    {
        LOG_FATAL("Failed to initialize Pico Constellation");
        ret = -1;
        goto failed;
    }

    char test_data[] = "KM7DEJ";

    HAL_timer_start(&intermittent_timer, 30 * ONE_SECOND); // Start timer for intermittent sending
    while (true)
    {
        pc_task(handle);
        if (HAL_timer_done(&intermittent_timer))
        {
            HAL_timer_reset(&intermittent_timer); // Reset timer for next send
            LOG_INFO("Sending test data...");
            if (pc_send_message(handle, 0x02, test_data, sizeof(test_data) -1)) // Send test data to address 0x02
            {
                LOG_ERROR("Failed to send test data");
            }
        }
    }

failed:
    return ret;
}
