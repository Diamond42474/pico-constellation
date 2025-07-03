#ifndef GOERTZEL_H
#define GOERTZEL_H

#include <stdint.h>

int goertzel_init(void);
int goertzel_deinit(void);

int goertzel_compute_power(const uint16_t *samples, int num_samples, float target_freq, float sample_rate, float *power);

#endif // GOERTZEL_H