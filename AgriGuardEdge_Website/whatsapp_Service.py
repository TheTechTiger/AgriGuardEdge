import subprocess
import json
import requests
import time
from datetime import datetime, timedelta

# Import DB helpers from database.py
import sys
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from database import SQL_Get_ASSOC_DATA, SQL_SetData

OLLAMA_URL = "http://localhost:11434/api/generate"
WHATSAPP_TO = os.environ.get("WHATSAPP_TO", "")
ESP_ID = os.environ.get("ESP_ID", "3C:8A:1F:76:FB:C4")

def get_whatsapp_messages():
    command = [
        "./wacli",
        "--store", "./wacliSync",
        "messages", "list",
        "--chat", os.environ.get("WHATSAPP_CHAT_ID", ""),
        "--json"
    ]
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        messages_dict = json.loads(result.stdout)
        return messages_dict
    except subprocess.CalledProcessError as e:
        print(f"[wacli] Command failed: {e.stderr}")
        return None
    except json.JSONDecodeError as e:
        print(f"[wacli] Failed to parse JSON: {e}")
        return None


def send_whatsapp_message(message: str):
    """Send a WhatsApp message using the wacli binary."""
    command = [
        "./wacli",
        "--store", "./wacliSync",
        "send", "text",
        "--to", WHATSAPP_TO,
        "--message", message
    ]
    try:
        result = subprocess.run(command, capture_output=True, text=True, check=True)
        print(f"[wacli] Message sent: {result.stdout.strip()}")
    except subprocess.CalledProcessError as e:
        print(f"[wacli] Failed to send message: {e.stderr}")


def get_serviced_msgs():
    try:
        with open("serviced_messages.txt", "r") as f:
            return f.read().splitlines()
    except FileNotFoundError:
        return []

def mark_msg_serviced(msg_id: str):
    with open("serviced_messages.txt", "a") as f:
        f.write(msg_id + "\n")

def get_esp_id():
    """Return the ESP MAC address registered in the DB."""
    try:
        row = SQL_Get_ASSOC_DATA("SELECT macADD FROM espDetails LIMIT 1")
        return row[0]["macADD"] if row else ESP_ID
    except Exception:
        return ESP_ID


def get_farm_info():
    """Fetch latest sensor reading + ESP last-active + relay status."""
    esp_id = get_esp_id()
    logs = SQL_Get_ASSOC_DATA(
        f"SELECT temperature, humidity, moisture, water_level, relay, entry_timestamp "
        f"FROM esp_logs WHERE espID='{esp_id}' ORDER BY entry_timestamp DESC LIMIT 1"
    )
    if not logs:
        return None, esp_id

    log = logs[0]
    esp_row = SQL_Get_ASSOC_DATA(
        f"SELECT lastActive FROM espDetails WHERE macADD='{esp_id}'"
    )
    last_active_str = esp_row[0]["lastActive"] if esp_row else None
    try:
        last_active_dt = datetime.strptime(last_active_str, "%Y-%m-%d %H:%M:%S")
        diff_minutes = (datetime.now() - last_active_dt).total_seconds() / 60
        status = "online" if diff_minutes <= 5 else "offline"
    except Exception:
        status = "offline"
        last_active_str = "unknown"

    pump_status = "ON" if log.get("relay") else "OFF"
    info = {
        "temperature": log.get("temperature", "N/A"),
        "humidity": log.get("humidity", "N/A"),
        "water_level": log.get("water_level", "N/A"),
        "moisture": log.get("moisture", "N/A"),
        "pump": pump_status,
        "esp_last_active": last_active_str,
        "esp_status": status,
    }
    return info, esp_id


def format_farm_info(info: dict) -> str:
    return (
        f"The farm stats are as follows:\n"
        f"Temperature: {info['temperature']}°C\n"
        f"Humidity: {info['humidity']}%\n"
        f"Water Level: {info['water_level']}%\n"
        f"Soil Moisture: {info['moisture']}%\n"
        f"Pump: {info['pump']}\n"
        f"ESP last active time: {info['esp_last_active']} ({info['esp_status']})"
    )


def insert_irrigation_rule(esp_id: str, active_start: str, active_end: str, rule: dict) -> str:
    """Insert an irrigation rule into the DB and return a confirmation message."""
    rule_json = json.dumps(rule).replace("'", "''")
    SQL_SetData(
        f"INSERT INTO irrigation_rules (espID, ActiveStart, ActiveEnd, Rule) "
        f"VALUES ('{esp_id}', '{active_start}', '{active_end}', '{rule_json}')"
    )
    rule_name = rule.get("rule_name", "Irrigation Rule")
    return f"✅ {rule_name} has been scheduled successfully."

def classify_message(user_message: str) -> dict:
    """Ask Ollama to parse the user message into a structured intent.
    Returns one of:
      {"intent": "get_info"}
      {"intent": "irrigate_schedule", "espID": "...", "ActiveStart": "...", "ActiveEnd": "...", "Rule": {...}}
      {"intent": "unknown", "response": "...short fallback reply..."}
    """
    now = datetime.now()
    now_str = now.strftime("%Y-%m-%d %H:%M:%S")
    end_10d = (now + timedelta(days=10)).strftime("%Y-%m-%d %H:%M:%S")
    esp_id = get_esp_id()

    curr_day = (now.weekday() + 1) % 7   # Python Mon=0, we use Sun=0 scheme

    system_prompt = f"""You are KrishiMitra, an AI assistant for a smart farm irrigation system.
        Current date and time (IST): {now_str}
        ESP MAC address: {esp_id}
        Current day-of-week (0=Sun,1=Mon,...,6=Sat): {curr_day}

        Your job is to parse the user's WhatsApp message and return ONLY a valid JSON object (no markdown, no extra text).

        The JSON must be exactly ONE of the following shapes:

        1. Farm info request (user asks about current farm/sensor status, weather, pump state, etc.):
        {{"intent": "get_info"}}

        2. Irrigation schedule request (user wants to irrigate now or schedule irrigation):
        {{
        "intent": "irrigate_schedule",
        "espID": "{esp_id}",
        "ActiveStart": "<YYYY-MM-DD HH:MM:SS>",
        "ActiveEnd": "<YYYY-MM-DD HH:MM:SS>",
        "Rule": {{
            "rule_name": "[WH]<short name>",
            "rule_type": "periodic",
            "days": [<day numbers>],
            "start_time": "<HHMMSS>",
            "end_time": "<HHMMSS>"
        }}
        }}
        - For "Irrigate now for X min/sec", set ActiveStart={now_str}, compute start_time as current time in HHMMSS, add duration for end_time, set days=[{curr_day}], ActiveEnd to same as end_time from now.
        - Adjust ActiveEnd appropriately for scheduled/recurring irrigation (up to {end_10d}).

        3. Anything else (greetings, unrelated topics, unknown requests):
        {{
        "intent": "unknown",
        "response": "<short reply text>"
        }}
        - For greetings like 'hello', respond with: Hey! I'm KrishiMitra, your farming companion 🌱 How can I help?
        - For off-topic requests like coding, recipes, etc.: Sorry, I can only assist with farm-related queries.
        - For anything unclear: Sorry, I didn't understand that request. You can ask me about your farm stats or to irrigate.

        RULES:
        - Output ONLY the JSON. No extra text, no markdown, no code fences.
        - All datetime strings use format YYYY-MM-DD HH:MM:SS.
        - time fields (start_time, end_time) are 6-digit strings: HHMMSS.
        """

    payload = {
        "model": "deepseek-v3.2:cloud", 
        "prompt": f"{system_prompt}\n\nUser message: {user_message}\n\nJSON:",
        "stream": False
    }
    try:
        resp = requests.post(OLLAMA_URL, json=payload, timeout=500)
        resp.raise_for_status()
        raw = resp.json().get("response", "").strip()
        import re
        raw = re.sub(r"```[\w]*\n?", "", raw).strip()
        match = re.search(r"\{.*\}", raw, re.DOTALL)
        if match:
            result = json.loads(match.group(0))
            return result
        else:
            print(f"[Ollama] Could not find JSON in response: {raw}")
    except Exception as e:
        print(f"[Ollama] Error: {e}")
    return {"intent": "unknown", "response": "Sorry, I'm having trouble processing your request right now. Please try again later."}

def process_message(msg_text: str) -> str:
    """Process a single WhatsApp message text. Returns the response string to send back."""
    intent_data = classify_message(msg_text)
    intent = intent_data.get("intent", "unknown")

    if intent == "get_info":
        info, esp_id = get_farm_info()
        if info:
            return format_farm_info(info)
        else:
            return "No sensor data available yet. Please ensure your ESP device is connected."

    elif intent == "irrigate_schedule":
        try:
            esp_id = intent_data.get("espID", get_esp_id())
            active_start = intent_data.get("ActiveStart")
            active_end = intent_data.get("ActiveEnd")
            rule = intent_data.get("Rule", {})
            if not rule or not active_start:
                return "Sorry, I couldn't figure out the irrigation schedule details. Please be more specific (e.g. 'Irrigate now for 5 minutes')."
            try:
                start_t = datetime.strptime(rule.get("start_time", "000000"), "%H%M%S")
                end_t   = datetime.strptime(rule.get("end_time",   "000000"), "%H%M%S")
                delta_sec = int((end_t - start_t).total_seconds())
                if delta_sec <= 0:
                    delta_sec += 86400          # handle midnight wrap-around
                active_start_dt = datetime.strptime(active_start, "%Y-%m-%d %H:%M:%S")
                active_end      = (active_start_dt + timedelta(seconds=delta_sec)).strftime("%Y-%m-%d %H:%M:%S")
            except Exception as dur_err:
                print(f"[process_message] Could not compute active_end from rule times: {dur_err}")
                active_end = intent_data.get("ActiveEnd", active_start)

            confirmation = insert_irrigation_rule(esp_id, active_start, active_end, rule)

            try:
                start_dt = datetime.strptime(rule.get("start_time", "000000"), "%H%M%S")
                end_dt = datetime.strptime(rule.get("end_time", "000000"), "%H%M%S")
                delta_sec = int((end_dt - start_dt).total_seconds())
                if delta_sec < 0:
                    delta_sec += 86400
                if delta_sec < 60:
                    duration_str = f"{delta_sec} seconds"
                else:
                    duration_str = f"{delta_sec // 60} minute(s)"
            except Exception:
                duration_str = "the scheduled duration"

            rule_name = rule.get("rule_name", "Manual Irrigation")
            return f"💧 {rule_name} started for {duration_str}.\n{confirmation}"

        except Exception as e:
            print(f"[process_message] irrigate_schedule error: {e}")
            return "Sorry, something went wrong while scheduling irrigation. Please try again."
    else:
        return intent_data.get("response", "Sorry, I can only assist with farm-related queries.")

if __name__ == "__main__":
    print("[KrishiMitra WhatsApp Service] Starting polling loop...")
    while True:
        try:
            data = get_whatsapp_messages()
            if data is None:
                time.sleep(5)
                continue
            msgs = data.get("data", {}).get("messages", [])
            serviced_msgs = get_serviced_msgs()
            for msg in msgs:
                msg_id = msg.get("MsgID")
                msg_text = msg.get("Text", "").strip()
                if not msg_id or not msg_text:
                    continue
                if msg_id in serviced_msgs:
                    continue
                print(f"[New Message] {msg_id}: {msg_text}")
                response = process_message(msg_text)
                print(f"[Response] {response}")
                send_whatsapp_message(response)
                mark_msg_serviced(msg_id)
        except Exception as e:
            print(f"[Polling loop error] {e}")
        os.system("./wacli --store ./wacliSync sync --once --idle-exit 2s")