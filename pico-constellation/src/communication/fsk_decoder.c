#include "fsk_decoder.h"

#include "goertzel.h"
#include "c-logger.h"

int fsk_decoder_init(void);
int fsk_decoder_deinit(void);

int fsk_decoder_set_bit_callback(void (*callback)(int bit));
int fsk_decoder_set_baud_rate(int baud_rate);
int fsk_decoder_set_sample_rate(int sample_rate);
int fsk_decoder_set_frequencies(float freq_0, float freq_1);
int fsk_decoder_set_power_threshold(float threshold);
int fsk_decoder_process(void);