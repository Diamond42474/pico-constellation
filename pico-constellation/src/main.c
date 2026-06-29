#include "pico/stdlib.h"
#include "peregrine-constellation.h"
#include "network/network.h"
#include "c-logger.h"

static int count = 0;
pc_handle_t *pc_handle;
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

int main(void)
{
    stdio_init_all();

    log_init(LOG_LEVEL_INFO);

    if (network_init())
    {
        LOG_ERROR("Failed to initialize network interface");
        return -1;
    }
    pc_handle = pc_init(data_callback);
    if (!pc_handle)
    {
        LOG_ERROR("Failed to initialize Peregrine Constellation");
        return -1;
    }

    while (1)
    {
        pc_task(pc_handle);
    }
    return 0;
}