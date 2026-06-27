#include <math.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"

#define PWM_PIN            14
#define PWM_WRAP           255

#define PWM_CLOCK_HZ       250000.0f      // PWM carrier frequency
#define TABLE_SIZE         256

static uint8_t sine_table[TABLE_SIZE];

static volatile float phase = 0.0f;
static volatile float phase_increment = 0.0f;

static repeating_timer_t sample_timer;

void pwm_sine_set_frequency(float frequency)
{
    phase_increment = (frequency * TABLE_SIZE) / PWM_CLOCK_HZ;
}

bool sample_timer_callback(repeating_timer_t *rt)
{
    uint8_t value = sine_table[(int)phase];

    pwm_set_gpio_level(PWM_PIN, value);

    phase += phase_increment;
    if (phase >= TABLE_SIZE)
        phase -= TABLE_SIZE;

    return true;
}

void pwm_sine_init(void)
{
    // Build sine lookup table
    for (int i = 0; i < TABLE_SIZE; i++)
    {
        float angle = (2.0f * M_PI * i) / TABLE_SIZE;
        float s = sinf(angle);

        sine_table[i] = (uint8_t)((s + 1.0f) * 127.5f);
    }

    gpio_set_function(PWM_PIN, GPIO_FUNC_PWM);

    uint slice = pwm_gpio_to_slice_num(PWM_PIN);

    pwm_config cfg = pwm_get_default_config();

    float div = (float)clock_get_hz(clk_sys) /
                (PWM_CLOCK_HZ * (PWM_WRAP + 1));

    pwm_config_set_clkdiv(&cfg, div);
    pwm_config_set_wrap(&cfg, PWM_WRAP);

    pwm_init(slice, &cfg, true);

    pwm_set_gpio_level(PWM_PIN, PWM_WRAP / 2);

    // Update PWM every carrier period
    add_repeating_timer_us(
        -(1000000.0 / PWM_CLOCK_HZ),
        sample_timer_callback,
        NULL,
        &sample_timer);
}

int main()
{
    stdio_init_all();

    pwm_sine_init();

    while (true)
    {
        pwm_sine_set_frequency(1200.0f);
        sleep_ms(5000);

        pwm_sine_set_frequency(2200.0f);
        sleep_ms(5000);
    }
}