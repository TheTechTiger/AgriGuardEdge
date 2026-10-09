# Datasets

## Source (cite as)

La Trobe University, *Smart Agriculture and Plant Health Monitoring using
IoT*, Mendeley Data `65jxyrxv7b` (2024), CC BY 4.0 —
<https://data.mendeley.com/datasets/65jxyrxv7b>
(via the [IoTDataset catalog](https://iotdataset.com/data/smart-agriculture-plant-health-iot-sensors)).

If you use this project academically, cite the dataset above, not this repo.

## Contents

| Path | What |
|---|---|
| `originals/` | The 8 `.xlsx` ThingSpeak exports, verbatim from the download |
| `mendeley_merged.csv` | All 8 channels joined on `entry_id` (built by `merge_xlsx.py`) |
| `merge_xlsx.py` | One-time merge script (`pip install pandas openpyxl` to re-run) |

Channels present: `soil_moisture, env_temperature, env_humidity,
soil_temperature, light_intensity, soil_pH, water_TDS,
solar_battery_voltage`. **Not present** (as stated in arch §1.4): flow,
irrigation on/off, anomaly labels — and despite the catalog page, this
download has **no `soil_humidity` file** (8 files, not 9).

## What the slice actually is — read before citing metrics

- **101 rows, one 30-minute window** (2024-09-27 12:58→13:28 IST, ~18 s
  cadence). It contains **no diurnal cycle**, so it cannot train or test
  the 24-bucket baseline — synthetic diurnal data remains the tuning
  source (`HardwarePIO/tools/anomaly_replay`, default 72 h).
- It contains **genuine transients**: a 61.88→31.48 single-sample plunge
  (row 2), a 64.22→38.42→59.92 multi-sample dip (entries 2301–2304).
  The detector flags these — correct behaviour, not false alarms — so a
  "false alarms/day" number is **not computable** on this slice (0.07 days
  of non-stationary data).
- Measured on this slice (`replay.py --csv datasets/mendeley_merged.csv`):
  **12/12 injections detected** (stress latency 3, rain silent, probe
  hydraulics all classify). Trailing rows have empty temperature cells;
  the loader falls back to defaults for those.

## Reproduce

```sh
cd AgriGuardEdge_HardwarePIO/tools/anomaly_replay
python3 replay.py --csv ../../../datasets/mendeley_merged.csv --inject all --report out/
```
