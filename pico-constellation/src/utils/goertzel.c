#include "goertzel.h"
#include <math.h>   // For cosf, M_PI
#include <stddef.h> // For NULL
#include "c-logger.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int goertzel_init(void)
{
    // No internal resources to allocate for now
    return 0;
}

int goertzel_deinit(void)
{
    // No internal resources to free for now
    return 0;
}

int goertzel_compute_power(const uint16_t *samples, int num_samples, float target_freq, float sample_rate, float *power)
{
    if (!samples || num_samples <= 0 || !power || sample_rate <= 0.0f || target_freq <= 0.0f)
    {
        LOG_ERROR("Invalid parameters: samples=%p, num_samples=%d, power=%p, sample_rate=%.2f, target_freq=%.2f",
                  (void *)samples, num_samples, (void *)power, sample_rate, target_freq);
        return -1; // Invalid parameters
    }

    float s_prev = 0.0f;
    float s_prev2 = 0.0f;

    float normalized_freq = target_freq / sample_rate;
    float coeff = 2.0f * cosf(2.0f * (float)M_PI * normalized_freq);

    for (int i = 0; i < num_samples; i++)
    {
        float s = (float)samples[i] + coeff * s_prev - s_prev2;
        s_prev2 = s_prev;
        s_prev = s;
    }

    *power = s_prev2 * s_prev2 + s_prev * s_prev - coeff * s_prev * s_prev2;

    return 0;
}
