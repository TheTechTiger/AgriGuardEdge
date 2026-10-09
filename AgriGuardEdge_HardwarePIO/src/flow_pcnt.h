/** Flow front-end: PCNT capture + volume integration (§5.1). Plain-C API. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

void flow_init(float pulses_per_litre);
void flow_set_ppl(float ppl);
bool flow_using_pcnt(void);          // false = ISR-poll degraded mode
void flow_power(bool on);            // gate the 15 mA sensor (§6.4)
void flow_reset_volume(void);
// Call each evaluation with dt_s since last call. Returns smoothed L/min.
float flow_poll(float dt_s);
float flow_volume_ml(void);          // ∫ since reset
uint32_t flow_pulses_total(void);
