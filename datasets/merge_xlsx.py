"""One-time merge of the 8 Mendeley xlsx exports into mendeley_merged.csv.
Needs: pip install pandas openpyxl. Re-run if originals/ changes.
Join key: entry_id (ThingSpeak exports share it across channels).
"""
import glob
import os
import pandas as pd

HERE = os.path.dirname(os.path.abspath(__file__))
ORIG = os.path.join(HERE, "originals", "Smart Agriculture and Plant Health Monitoring using IoT")

CHANNEL = {
    "Soil Moisture": "soil_moisture",
    "Environment Temperature": "env_temperature",
    "Environment Humidity": "env_humidity",
    "Soil Temperature": "soil_temperature",
    "Environment Light Intensity": "light_intensity",
    "Soil pH": "soil_pH",
    "Water TDS": "water_tds",
    "Solar Panel Battery Voltage": "solar_battery_voltage",
}

merged = None
for title, col in CHANNEL.items():
    df = pd.read_excel(os.path.join(ORIG, title + ".xlsx"), header=None)
    body = df.iloc[2:].copy()  # row0=title, row1=units
    body.columns = ["timestamp", "entry_id", "value"]
    body["entry_id"] = body["entry_id"].astype(int)
    body[col] = pd.to_numeric(body["value"], errors="coerce")
    body = body[["timestamp", "entry_id", col]].dropna()
    merged = body if merged is None else merged.merge(body, on=["timestamp", "entry_id"], how="outer")

merged = merged.sort_values("entry_id").reset_index(drop=True)
out = os.path.join(HERE, "mendeley_merged.csv")
merged.to_csv(out, index=False)
print(f"rows={len(merged)} cols={list(merged.columns)}")
print(merged.head(3).to_string())
print("span:", merged['timestamp'].iloc[0], "->", merged['timestamp'].iloc[-1])
print("wrote", out)
