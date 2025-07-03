#ifndef ADC_HAL_H
#define ADC_HAL_H

#include <stdint.h>
#include <stddef.h>

typedef void (*adc_buffer_ready_callback_t)(size_t size);

int adc_hal_init(void);
int adc_hal_deinit(void);

int adc_hal_set_sample_rate(int sample_rate);             // in Hz
int adc_hal_set_sample_size(int sample_size);             // minimumnumber of samples per callback
int adc_hal_set_callback(adc_buffer_ready_callback_t cb); // assign sample callback

int adc_hal_start(void);
int adc_hal_stop(void);

int adc_samples_available(int *num_samples); // Check how many samples are available in the buffer
int adc_hal_get_samples(uint16_t *buffer, size_t max_size, int *num_samples);

#endif // ADC_HAL_H