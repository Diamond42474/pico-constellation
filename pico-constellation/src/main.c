#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>

#include "mcp4725.h"

#include "peregrine-constellation.h"

int main()
{
    sleep_ms(2000);
    stdio_init_all();
    printf("Starting...\n");
    i2c_init(i2c0, 400 * 1000);          // Initialize I2C at 400 kHz
    gpio_set_function(4, GPIO_FUNC_I2C); // SDA
    gpio_set_function(5, GPIO_FUNC_I2C); // SCL
    gpio_pull_up(4);
    gpio_pull_up(5);

    mcp4725_handle_t dac;
    mcp4725_init(&dac, i2c0, 0x60);

    while (1)
    {
        // Generate sine wave
        for (int i = 0; i < 360; i++)
        {
            float voltage = 1.65 + 1.65 * sin(i * 3.14159 / 180);
            mcp4725_set_voltage(&dac, voltage, MCP4725_PowerDownOff);
            //sleep_us(10);
        }
    }

    return 0;
}