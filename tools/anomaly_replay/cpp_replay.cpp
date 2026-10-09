/* cpp_replay — runs the REAL firmware/common/anomaly_core.h on the host.
 * Input (stdin, one eval per line):
 *   t hour moisture temp rh flow pump adc vbat
 * NaN temp/rh = DHT dropout. Output (stdout CSV):
 *   idx,event,severity,state,z,cusum_up,cusum_down   (only on change rows
 *   where health or step emits non-NONE, plus final state line)
 * Build: g++ -std=c++17 -Wall -O2 -I../../firmware/common cpp_replay.cpp -o cpp_replay
 */
#include <cstdio>
#include <cmath>
#include "anomaly_core.h"

int main() {
    ag_core_t c;
    ag_config_t cfg = ag_config_default();
    ag_init(&c);
    long idx = 0;
    double t, temp, rh, flow, vbat;
    int hour, pump, adc;
    float moisture;
    printf("idx,event,severity,state,z,cu,cd\n");
    while (scanf("%lf %d %f %lf %lf %lf %d %d %lf",
                 &t, &hour, &moisture, &temp, &rh, &flow, &pump, &adc, &vbat) == 9) {
        ag_sample_t s;
        s.moisture_pct = moisture; s.adc_raw = adc;
        s.temp_c = (float)temp; s.rh_pct = (float)rh;
        s.flow_lpm = (float)flow; s.vbat_v = (float)vbat;
        s.pump_on = (uint8_t)(pump ? 1 : 0);
        s.t_sec = (uint32_t)t; s.hour = (uint8_t)(hour % 24);
        float innov = 0.f;
        ag_event_t ev = ag_health_gate(&c, &s, &innov);
        const char *ename = "NONE";
        float z = 0.f;
        if (ev == AG_EV_NONE || ev == AG_EV_SENSOR_TIMEOUT) {
            ag_result_t r = ag_step(&c, &cfg, &s, 60.f);
            z = r.z_soil;
            if (ev == AG_EV_SENSOR_TIMEOUT) {
                ename = ag_event_name(ev);
                printf("%ld,%s,INFO,%d,%.3f,%.3f,%.3f\n",
                       idx, ename, (int)c.state, z, r.cusum_up, r.cusum_down);
            }
            if (r.event != AG_EV_NONE) {
                printf("%ld,%s,%s,%d,%.3f,%.3f,%.3f\n",
                       idx, ag_event_name(r.event), ag_sev_name(r.severity),
                       (int)r.state, z, r.cusum_up, r.cusum_down);
            }
        } else {
            ag_window_push(&c, s.moisture_pct);
            printf("%ld,%s,%s,%d,%.3f,%.3f,%.3f\n",
                   idx, ag_event_name(ev),
                   (ev == AG_EV_SENSOR_SPIKE_BURST) ? "LOW" : "MED",
                   (int)c.state, z, c.cusum_up, c.cusum_down);
        }
        idx++;
    }
    printf("final,%s,%d\n", c.state == 2 ? "ALARM" : (c.state == 1 ? "SUSPECT" : "NORMAL"), (int)c.state);
    return 0;
}
