#ifndef AD9833_H
#define AD9833_H

typedef enum
{
    AD9833_MODE_SINE,
    AD9833_MODE_TRIANGLE,
    AD9833_MODE_SQUARE,
    AD9833_MODE_SLEEP
} ad9833_mode_t;

int ad9833_init(void);
int ad9833_deinit(void);

int ad9833_set_mode(ad9833_mode_t mode);
int ad9833_set_frequency_hz(float frequency);

#endif // AD9833_H