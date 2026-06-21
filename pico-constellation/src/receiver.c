#include <stdio.h>
#include "pico/stdlib.h"
#include "c-logger.h"
#include "HAL_time.h"
#include "peregrine-constellation.h"
#include "hardware/watchdog.h"
static HAL_timer_t intermittent_timer;

static int count = 0;
void data_callback(const uint8_t *data, size_t len, uint8_t src_addr)
{
    LOG_INFO("%d Decoded Data from %d: ", count++, src_addr);
    for (size_t i = 0; i < len; i++)
    {
        printf("%02X ", data[i]);
    }
    printf("\n");
    for (size_t i = 0; i < len; i++)
    {
        printf("%c", data[i]);
    }
    printf("\n");
}

int main()
{
    int ret = 0;
    stdio_init_all();
    sleep_ms(2000); // Wait for USB to initialize
    log_init(LOG_LEVEL_INFO);
    LOG_INFO("Booting Pico Constellation...");

    pc_handle_t *handle = pc_init(data_callback);
    if (!handle)
    {
        LOG_FATAL("Failed to initialize Pico Constellation");
        ret = -1;
        goto failed;
    }

    char test_data[] = "Hello!";

    HAL_timer_start(&intermittent_timer, 3 * ONE_SECOND); // Start timer for intermittent sending
    // Setup watchdog for 250ms
    watchdog_enable(250, 1);
    while (true)
    {
        pc_task(handle);
        watchdog_update(); // Feed the watchdog to prevent reset
    }

failed:
    return ret;
}

// 02 48 00