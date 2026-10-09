/* Native unit tests for anomaly_core.h — host g++, no Arduino, no Unity.
 * Build: g++ -std=c++17 -Wall -Wextra -I../common test_core.cpp -o test_core && ./test_core
 */
#include <cstdio>
#include <cmath>
#include <cstring>
#include "../common/anomaly_core.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); failures++; } \
    else { printf("ok: %s\n", msg); } \
} while (0)

static ag_sample_t mk(float moist, float flow, uint8_t pump, uint32_t t, uint8_t h) {
    ag_sample_t s;
    memset(&s, 0, sizeof(s));
    s.moisture_pct = moist; s.adc_raw = 2000;
    s.temp_c = 26.0f; s.rh_pct = 60.0f;
    s.flow_lpm = flow; s.vbat_v = 3.9f;
    s.pump_on = pump; s.t_sec = t; s.hour = h;
    return s;
}

int main() {
    ag_config_t cfg = ag_config_default();

    /* 1. median / MAD helpers */
    {
        float x[5] = {5, 1, 3, 2, 4};
        float tmp[AG_BASELINE_WIN];
        CHECK(ag_median(x, 5, tmp) == 3.0f, "median of 1..5 is 3");
        float y[4] = {10, 10, 10, 10};
        CHECK(ag_mad_sigma(y, 4, tmp) == AG_SIGMA_FLOOR, "flat window hits sigma floor");
    }

    /* 2. health gate: rails / range / stuck / spike / flow / battery */
    {
        ag_core_t c; ag_init(&c);
        ag_sample_t s = mk(50, 0, 0, 0, 8);
        s.moisture_pct = 150.0f;
        CHECK(ag_health_gate(&c, &s, nullptr) == AG_EV_SENSOR_OUT_OF_RANGE, "out-of-range trips");
        s.moisture_pct = 50; s.adc_raw = 4095;
        CHECK(ag_health_gate(&c, &s, nullptr) == AG_EV_SENSOR_DISCONNECTED, "rail reads disconnect");
        s.adc_raw = 2000; s.flow_lpm = 99.0f;
        CHECK(ag_health_gate(&c, &s, nullptr) == AG_EV_FLOW_IMPLAUSIBLE, "flow implausible trips");
        s.flow_lpm = 0; s.vbat_v = 3.1f;
        CHECK(ag_health_gate(&c, &s, nullptr) == AG_EV_POWER_LOW, "low battery trips");
    }

    /* 3. stuck: feed identical values, expect SENSOR_STUCK after K=8 */
    {
        ag_core_t c; ag_init(&c);
        ag_event_t ev = AG_EV_NONE;
        for (int i = 0; i < 12; i++) {
            ag_sample_t s = mk(41.8f, 0, 0, (uint32_t)i * 60, 8);
            float innov = 0;
            ev = ag_health_gate(&c, &s, &innov);
            ag_window_push(&c, s.moisture_pct);
            // step baseline too so windows advance like firmware does
            ag_config_t cc = ag_config_default();
            ag_step(&c, &cc, &s, 60);
        }
        CHECK(ev == AG_EV_SENSOR_STUCK, "flatline yields SENSOR_STUCK");
    }

    /* 4. diurnal baseline learns without false alarm on slow drift */
    {
        ag_core_t c; ag_init(&c);
        ag_result_t r{};
        for (int i = 0; i < 40; i++) {
            float m = 42.0f + 0.02f * i; // slow drift inside band
            ag_sample_t s = mk(m, 0, 0, (uint32_t)i * 60, 8);
            ag_health_gate(&c, &s, nullptr);
            r = ag_step(&c, &cfg, &s, 60);
        }
        CHECK(c.state == AG_ST_NORMAL, "slow in-band drift stays NORMAL");
        CHECK(r.event == AG_EV_NONE, "no event on healthy drift");
    }

    /* 5. sustained 3-sigma shift promotes NORMAL->SUSPECT->ALARM */
    {
        ag_core_t c; ag_init(&c);
        // train bucket 14
        for (int i = 0; i < 20; i++) {
            ag_sample_t s = mk(42.0f + ((i % 2) ? 0.2f : -0.2f), 0, 0, (uint32_t)i * 60, 14);
            ag_step(&c, &cfg, &s, 60);
        }
        ag_result_t r{};
        uint32_t t = 20 * 60;
        for (int i = 0; i < 12; i++) {
            t += 60;
            ag_sample_t s = mk(42.0f - 4.0f, 0, 0, t, 14); // ~-4sigma sustained
            r = ag_step(&c, &cfg, &s, 60);
        }
        CHECK(c.state == AG_ST_ALARM, "sustained shift reaches ALARM");
        CHECK(r.event == AG_EV_WATER_STRESS, "drying shift is WATER_STRESS");
        CHECK(r.severity == AG_SEV_HIGH, "confirmed + |z|>3 is HIGH");
    }

    /* 6. rain negative control: big +step must NOT alert */
    {
        ag_core_t c; ag_init(&c);
        for (int i = 0; i < 20; i++) {
            ag_sample_t s = mk(42.0f, 0, 0, (uint32_t)i * 60, 10);
            ag_step(&c, &cfg, &s, 60);
        }
        ag_sample_t rain = mk(52.0f, 0, 0, 21 * 60, 10);
        ag_result_t r = ag_step(&c, &cfg, &rain, 60);
        CHECK(r.event == AG_EV_EXTERNAL_EVENT, "rain step is EXTERNAL_EVENT");
        CHECK(c.state == AG_ST_NORMAL, "rain does not promote state");
    }

    /* 7. continuous leak: pump OFF + flow for 2 windows */
    {
        ag_core_t c; ag_init(&c);
        ag_sample_t s1 = mk(42, 2.5f, 0, 60, 10);
        ag_result_t r1 = ag_step(&c, &cfg, &s1, 60);
        CHECK(r1.event == AG_EV_NONE, "first leak window arms, no alert yet");
        ag_sample_t s2 = mk(42, 2.5f, 0, 120, 10);
        ag_result_t r2 = ag_step(&c, &cfg, &s2, 60);
        CHECK(r2.event == AG_EV_CONTINUOUS_LEAK, "second window fires CONTINUOUS_LEAK");
        CHECK(r2.severity == AG_SEV_HIGH, "continuous leak is HIGH");
    }

    /* 8. blockage: pump ON + ~0 flow for >=15 s */
    {
        ag_core_t c; ag_init(&c);
        ag_result_t r{};
        for (int i = 0; i < 4; i++) {
            ag_sample_t s = mk(42, 0.0f, 1, (uint32_t)(i + 1) * 10, 10);
            r = ag_step(&c, &cfg, &s, 10);
        }
        CHECK(r.event == AG_EV_BLOCKAGE_NO_FLOW, "pump on + no flow is BLOCKAGE_NO_FLOW");
    }

    /* 9. conservation bands (§5.6) incl. drift-impossible direction */
    {
        CHECK(ag_conservation_verdict(0.90f, 250) == AG_EV_IRRIGATION_OK, "0.90 ok");
        CHECK(ag_conservation_verdict(0.60f, 250) == AG_EV_BLOCKAGE_PARTIAL, "0.60 partial");
        CHECK(ag_conservation_verdict(0.30f, 250) == AG_EV_LEAK_OR_BYPASS, "0.30 leak");
        CHECK(ag_conservation_verdict(0.01f, 250) == AG_EV_LEAK_SEVERE, "0.01 severe");
        CHECK(ag_conservation_verdict(1.40f, 250) == AG_EV_OVER_IRRIGATION, "1.40 over");
    }

    /* 10. probe evaluate: reference calibrates, later leak detected */
    {
        ag_core_t c; ag_init(&c);
        ag_result_t r{};
        ag_event_t v = ag_probe_evaluate(&c, &cfg, 250, 5.0f, 3.0f, 1, &r);
        CHECK(v == AG_EV_IRRIGATION_OK && c.r_ref > 0.001f, "reference cycle sets R_ref");
        float good_soil = c.r_ref * 250.0f / cfg.v_wetted_ml; // d_moist of a good cycle
        ag_event_t v2 = ag_probe_evaluate(&c, &cfg, 250, good_soil * 0.2f, 3.0f, 0, &r);
        CHECK(v2 == AG_EV_LEAK_OR_BYPASS || v2 == AG_EV_LEAK_SEVERE, "collapsed retention is a leak");
        ag_event_t v3 = ag_probe_evaluate(&c, &cfg, 250, good_soil * 1.8f, 3.0f, 0, &r);
        CHECK(v3 == AG_EV_SENSOR_DRIFT, "impossible retention is SENSOR_DRIFT");
    }

    /* 11. cadence policy */
    {
        CHECK(ag_cadence_s(AG_ST_NORMAL, 0) == 60, "normal cadence 60 s");
        CHECK(ag_cadence_s(AG_ST_SUSPECT, 0) == 10, "suspect cadence 10 s");
        CHECK(ag_cadence_s(AG_ST_ALARM, 0) == 5, "alarm cadence 5 s");
        CHECK(ag_cadence_s(AG_ST_NORMAL, 1) == 5, "probe cadence 5 s");
    }

    if (failures == 0) printf("\nALL TESTS PASSED\n");
    else printf("\n%d FAILURES\n", failures);
    return failures ? 1 : 0;
}
