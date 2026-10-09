# Architecture.md

# AgriGuard Edge
## Edge-Native Irrigation Anomaly Detection for Battery-Powered Smart Agriculture

---

# Problem Statement

Traditional smart agriculture deployments continuously transmit sensor readings to the cloud.

This causes:

- High battery consumption
- Excess network traffic
- Increased cloud processing costs
- Delayed anomaly detection

The objective is to detect irrigation anomalies locally on a battery-powered edge node and transmit only critical information.

---

# Solution Overview

AgriGuard Edge performs anomaly detection directly on an ESP32-S3.

Instead of transmitting every sensor reading, the device:

1. Collects sensor data
2. Maintains a small rolling feature window
3. Computes anomaly scores locally
4. Sends only:
   - Anomaly events
   - Periodic summaries

This dramatically reduces communication requirements while enabling real-time response.

---

# High-Level Architecture

```text

                    ┌────────────────────┐
                    │ Soil Moisture      │
                    └─────────┬──────────┘

                    ┌────────────────────┐
                    │ DHT22              │
                    │ Temp + Humidity    │
                    └─────────┬──────────┘

                    ┌────────────────────┐
                    │ Flow Sensor        │
                    └─────────┬──────────┘

                              │
                              ▼

        ┌─────────────────────────────────────┐
        │            ESP32-S3 N16R8           │
        │                                     │
        │ Sensor Acquisition                  │
        │ Rolling Window Buffer               │
        │ Feature Extraction                  │
        │ Edge Anomaly Engine                 │
        │ Local Decision Logic                │
        └──────────────┬──────────────────────┘
                       │
          ┌────────────┴────────────┐
          │                         │
          ▼                         ▼

      Normal                    Anomaly

 Aggregate Summary         Immediate Alert

          │                         │
          └────────────┬────────────┘
                       │
                       ▼

              Flask REST API

                       │
                       ▼

          SQLite + Dashboard UI

```

---

# Hardware Architecture

## Controller

### ESP32-S3 N16R8

Responsibilities:

- Sensor sampling
- Feature computation
- Anomaly detection
- WiFi communication
- Power-efficient operation

---

## Sensors

### Soil Moisture Sensor

Primary signal.

Used for:

- Dryness monitoring
- Irrigation effectiveness
- Leak detection

---

### DHT22

Provides:

- Air temperature
- Relative humidity

Used as contextual features.

---

### Flow Sensor (Recommended)

Used for:

- Leak detection
- Pipe burst detection
- Irrigation flow verification

---

### Relay + Pump (Optional Demo Upgrade)

Allows automatic corrective action.

Example:

```text
Leak detected
      ↓
Pump OFF
```

---

# Edge Processing Pipeline

## Step 1

Sensor Sampling

Every:

```text
30-60 seconds
```

Read:

```text
soil_moisture
temperature
humidity
flow_rate
```

---

## Step 2

Rolling Window

Store only recent measurements.

Example:

```cpp
window_size = 30
```

Maintain approximately:

```text
Last 30 samples
```

No long-term raw data storage.

No CSV files.

No local database.

---

## Step 3

Feature Extraction

Compute:

### Moisture Average

```text
avg_moisture
```

### Moisture Variance

```text
std_moisture
```

### Moisture Slope

Rate of change.

```text
d(moisture)/dt
```

### Average Temperature

```text
avg_temperature
```

### Average Humidity

```text
avg_humidity
```

### Flow Statistics

```text
avg_flow_rate
```

---

# Anomaly Engine

Hybrid Approach:

```text
Rule-Based Detection
+
Lightweight Statistical Model
```

---

## Rule Engine

### Leak Detection

Expected:

```text
flow > threshold
AND
moisture increasing
```

Actual:

```text
flow > threshold
AND
moisture not increasing
```

Classification:

```text
LEAK
```

---

### Irrigation Failure

Expected:

```text
pump ON
moisture rising
```

Actual:

```text
pump ON
moisture falling
```

Classification:

```text
IRRIGATION_FAILURE
```

---

### Sensor Failure

Condition:

```text
same value repeatedly
```

Example:

```text
42
42
42
42
42
42
```

Classification:

```text
SENSOR_STUCK
```

---

## Statistical Anomaly Score

Compute z-score style deviation.

Example:

```text
score =
| moisture - mean |
+
| temperature - mean |
+
| humidity - mean |
```

Threshold:

```text
score > threshold
```

↓

```text
ANOMALY
```

This avoids heavy ML deployment while still providing edge intelligence.

---

# Communication Strategy

## Traditional IoT

Every reading transmitted.

```text
1 minute interval

1440 readings/day

1440 transmissions/day
```

---

## AgriGuard Edge

Normal readings are aggregated.

Transmit:

```text
summary every 15 minutes
```

Result:

```text
96 summaries/day
```

---

Additionally:

```text
Immediate anomaly alerts
```

Expected:

```text
5-20 anomaly messages/day
```

Total:

```text
~100 transmissions/day
```

instead of:

```text
1440 transmissions/day
```

Reduction:

```text
93-99%
```

---

# REST API Architecture

## Alert Endpoint

```http
POST /api/alert
```

Example:

```json
{
  "device_id": "esp32_01",
  "timestamp": "2026-10-09T10:15:00",
  "event": "LEAK",
  "severity": "HIGH"
}
```

---

## Summary Endpoint

```http
POST /api/summary
```

Example:

```json
{
  "device_id": "esp32_01",
  "avg_moisture": 42.5,
  "avg_temp": 29.3,
  "avg_humidity": 63,
  "battery": 3.95
}
```

---

## Dashboard Endpoint

```http
GET /
```

Displays:

- Live status
- Recent alerts
- Network savings
- Device health

---

# Backend Stack

## Flask

Provides:

```text
REST API
Dashboard Routes
```

---

## SQLite

Stores:

### Alerts

```sql
id
timestamp
event
severity
```

### Summaries

```sql
id
timestamp
avg_moisture
avg_temp
avg_humidity
battery
```

---

# Dashboard Components

## Sensor Overview

```text
Moisture

Temperature

Humidity

Flow Rate
```

---

## Device Status

```text
ONLINE

HEALTHY

BATTERY OK
```

---

## Alerts

```text
LEAK DETECTED

SENSOR FAILURE

IRRIGATION FAILURE
```

---

## Communication Savings

```text
Readings Generated

Transmissions Sent

Reduction Percentage
```

Example:

```text
Generated: 1440

Sent: 110

Reduction: 92.4%
```

---

# Demo Scenarios

## Scenario 1

Normal Irrigation

Expected:

```text
Flow detected

Moisture increasing

No alerts
```

---

## Scenario 2

Leak Detection

Simulate:

```text
Water flowing
but not reaching soil
```

Observed:

```text
Flow present

Moisture unchanged
```

Result:

```text
LEAK ALERT
```

API call generated.

---

## Scenario 3

Sensor Failure

Disconnect sensor.

Observed:

```text
Repeated constant values
```

Result:

```text
SENSOR FAILURE ALERT
```

---

## Scenario 4

Irrigation Failure

Pump running.

Moisture continues dropping.

Result:

```text
IRRIGATION FAILURE ALERT
```

---

# Key Innovation

Instead of:

```text
Sensor → Cloud → Analysis
```

AgriGuard Edge performs:

```text
Sensor → Edge Analysis → Alert
```

Benefits:

- Reduced transmission frequency
- Lower power consumption
- Faster anomaly detection
- Improved scalability
- Suitable for battery-powered deployments

---

# Elevator Pitch

AgriGuard Edge is an ESP32-S3-based smart agriculture node that performs irrigation anomaly detection directly on the edge, identifies leaks, sensor failures, and irrigation faults in real time, and transmits only summaries and critical alerts to a Flask backend, reducing communication overhead by up to 99% while extending operational battery life.