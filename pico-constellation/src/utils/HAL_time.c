#include "HAL_time.h"

#include "pico/time.h"

uint64_t HAL_get_current_time_us(void)
{
    return to_us_since_boot(get_absolute_time());
}

uint32_t HAL_get_current_time_ms(void)
{
    return HAL_get_current_time_us() / 1000;
}

void HAL_timer_start(HAL_timer_t *timer, uint64_t wait)
{
    timer->start_time = HAL_get_current_time_us();
    timer->wait = wait;
}

bool HAL_timer_done(HAL_timer_t *timer)
{
    uint64_t current_time = HAL_get_current_time_us();
    return (current_time - timer->start_time) >= timer->wait;
}

void HAL_timer_reset(HAL_timer_t *timer)
{
    timer->start_time = HAL_get_current_time_us();
}