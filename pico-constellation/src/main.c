#include <stdio.h>
#include "pico/stdlib.h"

#include "nrf24_driver.h"
#include "peregrine-constellation.h"

int main() {
    stdio_init_all();

    while (1) {
        printf("Hello, Raspberry Pi Pico!\n");
        sleep_ms(1000);
    }

    return 0;
}