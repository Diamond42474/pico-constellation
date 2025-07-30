#ifndef HAL_TIME_H
#define HAL_TIME_H

#include <stdint.h>
#include <stdbool.h>

#define ONE_MILLISECOND 1000
#define ONE_SECOND ONE_MILLISECOND * 1000

uint64_t HAL_get_current_time_us(void);
uint32_t HAL_get_current_time_ms(void);

typedef struct {
    uint64_t start_time;
    uint64_t wait;
} HAL_timer_t;

void HAL_timer_start(HAL_timer_t *timer, uint64_t wait);
bool HAL_timer_done(HAL_timer_t *timer);
void HAL_timer_reset(HAL_timer_t *timer);

#endif // HAL_TIME_H