#include <stdio.h>
#include "pico/stdlib.h"
#include <math.h>
#include "mcp4725.h"

// Define constants
#define SINE_TABLE_SIZE 256   // Number of entries in the sine wave lookup table
#define M_PI 3.14159265358979323846
#define DAC_MAX_VALUE 4095    // 12-bit DAC resolution (0 to 4095)

// Precompute sine wave lookup table with raw 12-bit values
static uint16_t sine_table[SINE_TABLE_SIZE];

// Function to initialize the sine wave lookup table
void init_sine_table() {
    for (int i = 0; i < SINE_TABLE_SIZE; i++) {
        // Scale the sine wave value to the 12-bit DAC range (0-4095)
        sine_table[i] = (uint16_t)((sin((2 * M_PI * i) / SINE_TABLE_SIZE) + 1.0f) * 0.5f * DAC_MAX_VALUE);
    }
}

// Optimized sine wave generation using the lookup table
void generate_sine_wave(mcp4725_handle_t *dac, float frequency, float amplitude, uint32_t duration_us, uint32_t samples_per_second)
{
    if (amplitude < 0.0f || amplitude > 3.3f)
    {
        printf("Invalid amplitude. Must be between 0.0 and 3.3 volts.\n");
        return;
    }

    // Calculate total samples and sample delay
    uint32_t total_samples = (uint32_t)((duration_us / 1e6f) * samples_per_second);
    uint32_t sample_delay = (uint32_t)(1e6f / samples_per_second); // Microseconds per sample

    // Calculate frequency increment for table indexing
    float table_index_increment = (float)SINE_TABLE_SIZE * frequency / samples_per_second;

    // Iterate through samples
    float current_index = 0.0f;  // Initialize the current table index

    for (uint32_t i = 0; i < total_samples; i++)
    {
        // Find the corresponding sine value from the lookup table
        uint32_t table_index = (uint32_t)current_index % SINE_TABLE_SIZE;  // Wrap around if necessary
        uint16_t sine_value = sine_table[table_index];  // Directly fetch 12-bit sine value

        // Map sine value to the desired amplitude (scale the voltage)
        float voltage = (amplitude * (sine_value / (float)DAC_MAX_VALUE));  // Voltage mapped to [0, amplitude]

        // Set the DAC voltage using the raw value (12-bit)
        mcp4725_set_voltage_raw(dac, (uint16_t)(voltage * DAC_MAX_VALUE / 3.3f), MCP4725_PowerDownOff);

        // Update the index for the next sample
        current_index += table_index_increment;
        if (current_index >= SINE_TABLE_SIZE) {
            current_index -= SINE_TABLE_SIZE; // Ensure the index stays within bounds
        }

        // Sleep for the required sample delay
        sleep_us(sample_delay);
    }

    // Ensure smooth stop by outputting 0V
    mcp4725_set_voltage_raw(dac, 0, MCP4725_PowerDownOff);
}

int main()
{
    sleep_ms(2000);
    stdio_init_all();
    printf("Starting...\n");

    i2c_init(i2c0, 3400000);          // Initialize I2C at 400 kHz
    gpio_set_function(4, GPIO_FUNC_I2C); // SDA
    gpio_set_function(5, GPIO_FUNC_I2C); // SCL
    gpio_pull_up(4);
    gpio_pull_up(5);

    mcp4725_handle_t dac; // DAC handle
    mcp4725_init(&dac, i2c0, 0x60);

    // Initialize the sine wave lookup table
    init_sine_table();

    float frequency = 100.0f;            // 1 kHz
    float amplitude = 3.3f;              // 3V amplitude
    uint32_t duration_us = 500 * 1000;   // 5 seconds
    uint32_t samples_per_second = 20 * 1000; // 10 kHz sample rate

    while (1)
    {
        for (frequency = 100.0f; frequency <= 1000.0f; frequency += 100.0f)
        {
            generate_sine_wave(&dac, frequency, amplitude, duration_us, samples_per_second);
        }
    }

    return 0;
}
