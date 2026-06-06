#include "time_bsp.h"
#include "pico/time.h"

int time_bsp_init()
{
    return 0;
}

uint64_t time_bsp_get_ms()
{
    return time_bsp_get_us() / 1000;
}

uint64_t time_bsp_get_us()
{
    return to_us_since_boot(get_absolute_time());
}