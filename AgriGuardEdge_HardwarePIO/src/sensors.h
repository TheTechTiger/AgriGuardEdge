/** Gated soil/battery/DHT11 drivers. All sensors sleep by default. */
#pragma once
#include <stdint.h>

void sensors_init(void);
// Returns calibrated moisture % (or last good on transient error).
// Sets *adc_out to the raw 12-bit mean (-1 if unavailable).
float soil_read(int *adc_out);
float batt_read(void);                       // cell volts
bool  dht_read(float *temp_c, float *rh);    // false = timeout (NaN out)
void  pump_cmd(bool on);
