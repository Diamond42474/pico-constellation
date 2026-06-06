#include "ptt_bsp.h"
#include "pico/stdlib.h"

#define PTT_PIN 15 // GP15 for PTT control

int ptt_bsp_init()
{
    // Initialize GPPTT_PIN output and set it low
    gpio_init(PTT_PIN);
    gpio_set_dir(PTT_PIN, GPIO_OUT);
    // set internal pull down resistor
    gpio_pull_down(PTT_PIN);
    gpio_put(PTT_PIN, 0);

    return 0;
}

int ptt_bsp_task()
{
    return 0;
}

int ptt_bsp_set_ptt(bool active)
{
    gpio_put(PTT_PIN, active ? 1 : 0);
    return 0;
}