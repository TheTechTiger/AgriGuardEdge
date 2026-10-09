# Project Source Code

## app.py

```python
from flask import jsonify, redirect, request, send_from_directory, render_template, session, Flask,Response,send_file, stream_with_context
import os, json, re
from database import *
from datetime import datetime,timedelta
import requests

# flask run --debug -h 0.0.0.0 -p 80
app = Flask("AgroSense")
app.secret_key = os.environ.get("SECRET_KEY", ".--secret-key--..")

if os.path.exists(".env"):
    with open(".env") as f:
        for line in f:
            if "=" in line:
                key, value = line.strip().split("=", 1)
                os.environ[key] = value

def get_readable_rules(UserID):
    ESPid = SQL_Get_ASSOC_DATA(f"SELECT espID FROM userDetails WHERE id={UserID}")[0]["espID"]
    data = SQL_Get_ASSOC_DATA(f"SELECT * FROM irrigation_rules WHERE espID='{ESPid}' AND ActiveEnd > datetime('now', 'localtime')")
    print(data)
    readable_rules = []
    # plan_name, start, end, rules
    for rule in data:
        rule["Rule"]=json.loads(rule["Rule"])
        if rule["Rule"]["rule_type"]=="periodic":
            days_map = {0: "Sun", 1: "Mon", 2: "Tue", 3: "Wed", 4: "Thu", 5: "Fri", 6: "Sat"}
            days_str = ", ".join([days_map.get(day, str(day)) for day in rule["Rule"]["days"]])
            start_time = f"{rule['Rule']['start_time'][:2]}:{rule['Rule']['start_time'][2:4]}:{rule['Rule']['start_time'][4:6]}"
            end_time = f"{rule['Rule']['end_time'][:2]}:{rule['Rule']['end_time'][2:4]}:{rule['Rule']['end_time'][4:6]}"
            readable_rule = f"Water every {days_str} from {start_time} to {end_time}"
        elif rule["Rule"]["rule_type"]=="moisture_based":
            readable_rule = f"Water when moisture goes below {rule['Rule']['min']}% till its {rule['Rule']['max']}%"
        elif rule["Rule"]["rule_type"]=="rain_delay":
            readable_rule = f"Irrigation paused due to rain from {rule['ActiveStart']} to {rule['ActiveEnd']}"
            rule["Rule"]["rule_name"] = "Rain Delay"
        else:
            readable_rule = f"{rule['Rule']['rule_name']}: Unknown rule type"
        readable_rules.append({
            "plan_name": rule["Rule"]["rule_name"],
            "start": rule["ActiveStart"],
            "end": rule["ActiveEnd"],
            "rules": readable_rule
        })
    return readable_rules

def Get_Weather():
    try:
        API_KEY = "7b7df640cfdd61d7fbe4a12e1b06cf51"
        if not API_KEY:
            return jsonify({"error": "API key not configured"}), 500
        result = SQL_Get_ASSOC_DATA(f"SELECT location FROM farmDetails WHERE userID={session['userID']}")
        if not result or not result[0].get("location"):
            return jsonify({"error": "Farm location not found"}), 404
        location = result[0]["location"]
        if "," not in location:
            return jsonify({"error": "Invalid location format"}), 400
        lat, lon = [x.strip() for x in location.split(",")]
        try:
            lat = float(lat)
            lon = float(lon)
        except ValueError:
            return jsonify({"error": "Invalid latitude/longitude"}), 400
        url = f"https://api.openweathermap.org/data/2.5/forecast?lat={lat}&lon={lon}&appid={API_KEY}&units=metric"
        response = requests.get(url, timeout=5)
        data = response.json()
        return data
    except requests.exceptions.Timeout:
        return jsonify({"error": "Weather API timeout"}), 504
    except requests.exceptions.RequestException:
        return jsonify({"error": "Failed to connect to Weather API"}), 502
    except Exception as e:
        return jsonify({"error": str(e)}), 500

# WEB ROUTES
@app.context_processor
def inject_globals():
    if "userID" not in session:
        return dict()
    profile_data = SQL_Get_ASSOC_DATA(f"SELECT userName, emailId, profilePic FROM userDetails WHERE id={session.get('userID', -1)}")[0]
    profile_data["profilePic"] = imageBytesToSrc(profile_data["profilePic"]) if profile_data["profilePic"] else "assets/img/pfp.png"
    notifications = SQL_Get_ASSOC_DATA(f"SELECT * FROM notifications WHERE userID={session.get('userID', -1)} ORDER BY dateTime LIMIT 5")
    return dict(profile_data=profile_data, notifications=notifications)
    
@app.route("/assets/<path:filename>")
def serve_static(filename):
    return send_from_directory("assets", filename)

@app.route("/")
def index():
    return redirect("/dashboard")

@app.route("/register")
def register():
    return render_template("register.html")

@app.route("/ContactUs")
def contact():
    return render_template("contact.html")

@app.route("/FAQ")
def faq():
    return render_template("faq.html")

@app.route("/login", methods=["GET", "POST"])
def login():
    if request.method == "POST":
        username = request.form["username"]
        password = request.form["password"]
        user = SQL_Get_ASSOC_DATA(f"SELECT * FROM userDetails WHERE emailId='{username}'")
        if user and user[0]["password"] == password:
            session["userID"] = user[0]["id"]
            return redirect("/dashboard")
        else:
            return "Invalid credentials", 401
    if "userID" in session:
        session.pop("userID")
        session.clear()
    return render_template("login.html")

# WEB ROUTES - AUTHENTICATED
@app.route("/dashboard")
def dashboard():
    if "userID" not in session:
        return redirect("/login")
    return render_template("dashboard.html", weather_data=Get_Weather(), irrigation_data= get_readable_rules(session["userID"]))

@app.route("/auto-irrigation")
def auto_irrigation():
    if "userID" not in session:
        return redirect("/login")
    return render_template("auto-irrigation.html")

@app.route("/manual-irrigation", methods=["GET", "POST"])
def manual_irrigation():
    if "userID" not in session:
        return redirect("/login")
    if request.method == "POST":
        duration = int(request.form.get("duration", 30))
        from datetime import datetime, timedelta
        now = datetime.now()
        if request.form.get("unit") == "seconds":
            end = now + timedelta(seconds=duration)
        elif request.form.get("unit") == "minutes":
            end = now + timedelta(minutes=duration)
        curr_day = (now.weekday() + 1) % 7
        rule_dict = {
            "rule_name": request.form.get("planName", "Manual Watering"),
            "rule_type": "periodic",
            "days": [curr_day],
            "start_time": now.strftime("%H%M%S"),
            "end_time": end.strftime("%H%M%S"),
        }
        import json
        rule_json = json.dumps(rule_dict).replace("'", "''")
        user_info = SQL_Get_ASSOC_DATA(f"SELECT espID FROM userDetails WHERE id={session['userID']}")
        espID = user_info[0]["espID"] if user_info else 'XX:XX:XX:XX:XX:XX'
        SQL_SetData(f"INSERT INTO irrigation_rules (espID, ActiveStart, ActiveEnd, Rule) VALUES ('{espID}', datetime('now', 'localtime'), '{end.strftime('%Y-%m-%d %H:%M:%S')}', '{rule_json}')")
        return f"<script>alert('Irrigation started for {duration} {request.form.get('unit')}!'); window.location='/irrigation-rules';</script>"
    return render_template("manual-irrigation.html")

@app.route("/periodic-irrigation", methods=["GET", "POST"])
def periodic_irrigation():
    if "userID" not in session:
        return redirect("/login")
    if request.method == "POST":
        plan_name = request.form.get("planName", "Periodic Watering")
        frequency = request.form.get("frequency")
        days = []
        if frequency == "daily":
            days = [0,1,2,3,4,5,6]
        elif frequency == "mon-fri":
            days = [1,2,3,4,5]
        else:
            day_map = {"monday": 1, "tuesday": 2, "wednesday": 3, "thursday": 4, "friday": 5, "saturday": 6, "sunday": 0}
            for i in range(7):
                day_val = request.form.get(f"day{i}")
                if day_val in day_map:
                    days.append(day_map[day_val])
        start_time_str = request.form.get("start_time", "06:00")
        duration_min = int(request.form.get("duration", 30))
        sh, sm = map(int, start_time_str.split(':'))
        eh = sh + (sm + duration_min) // 60
        em = (sm + duration_min) % 60
        eh = eh % 24
        rule_dict = {
            "rule_name": plan_name,
            "rule_type": "periodic",
            "days": days,
            "start_time": f"{sh:02d}{sm:02d}00",
            "end_time": f"{eh:02d}{em:02d}00",
        }
        import json
        rule_json = json.dumps(rule_dict).replace("'", "''")
        user_info = SQL_Get_ASSOC_DATA(f"SELECT espID FROM userDetails WHERE id={session['userID']}")
        espID = user_info[0]["espID"] if user_info else 'XX:XX:XX:XX:XX:XX'
        SQL_SetData(f"INSERT INTO irrigation_rules (espID, ActiveStart, ActiveEnd, Rule) VALUES ('{espID}', datetime('now', 'localtime'), datetime('now', 'localtime', '+10 days'), '{rule_json}')")
        return redirect("/irrigation-rules")
    return render_template("periodic-irrigation.html")

@app.route("/sensor-triggered-irrigation", methods=["GET", "POST"])
def sensor_triggered_irrigation():
    if "userID" not in session:
        return redirect("/login")
    if request.method == "POST":
        plan_name = request.form.get("planName", "Sensor Watering")
        min_threshold = int(request.form.get("min_threshold", 30))
        max_threshold = int(request.form.get("max_threshold", 70))
        rule_dict = {
            "rule_name": plan_name,
            "rule_type": "moisture_based",
            "min": min_threshold,
            "max": max_threshold,
        }
        rule_json = json.dumps(rule_dict).replace("'", "''")
        user_info = SQL_Get_ASSOC_DATA(f"SELECT espID FROM userDetails WHERE id={session['userID']}")
        espID = user_info[0]["espID"] if user_info else 'XX:XX:XX:XX:XX:XX'
        SQL_SetData(f"INSERT INTO irrigation_rules (espID, ActiveStart, ActiveEnd, Rule) VALUES ('{espID}', datetime('now', 'localtime'), datetime('now', 'localtime', '+10 days'), '{rule_json}')")
        return redirect("/irrigation-rules")
    return render_template("sensor-triggered-irrigation.html")

@app.route("/irrigation-rules") 
def irrigation_rules():
    if "userID" not in session:
        return redirect("/login")
    return render_template("irrigation-rules.html", rules_data=get_readable_rules(session["userID"]))

@app.route("/chat")
def chat():
    if "userID" not in session:
        return redirect("/login")
    return render_template("chat.html")

@app.route("/api/chat", methods=["POST"])
def api_chat():
    if "userID" not in session:
        return jsonify({"error": "Unauthorized"}), 401

    try:
        data = request.get_json()
        chat_history = data.get("history", [])

        # Ensure we don't duplicate the latest message if it's already at the end of history
        user_message = data.get("message")
        if chat_history and chat_history[-1].get("text") != user_message:
            chat_history.append({"sender": "user", "text": user_message})

        # Fetch Farm Details
        farm_details = SQL_Get_ASSOC_DATA(f"SELECT * FROM farmDetails WHERE userID={session['userID']}")
        farm_info = "No specific farm details available."
        if farm_details:
            farm_info = json.dumps(farm_details[0])

        # Fetch last 5 sensor readings
        esp_id = None
        try:
            esp_id = SQL_Get_ASSOC_DATA(f"SELECT espID FROM userDetails WHERE id={session['userID']}")[0]["espID"]
        except Exception:
            pass

        sensor_info = "No recent sensor data available."
        if esp_id:
            recent_logs = SQL_Get_ASSOC_DATA(
                f"SELECT temperature, humidity, water_level, moisture, entry_timestamp "
                f"FROM esp_logs WHERE espID='{esp_id}' ORDER BY entry_timestamp DESC LIMIT 5"
            )
            if recent_logs:
                rows = []
                for r in recent_logs:
                    rows.append(
                        f"  [{r['entry_timestamp']}] Temp: {r['temperature']}°C, "
                        f"Humidity: {r['humidity']}%, Water Level: {r['water_level']}, "
                        f"Moisture: {r['moisture']}%"
                    )
                sensor_info = "Last 5 sensor readings (newest first):\n" + "\n".join(rows)

        # Fetch current active irrigation rules
        rules_info = "No active irrigation rules."
        readable = get_readable_rules(session["userID"])
        if readable:
            rule_lines = []
            for ru in readable:
                rule_lines.append(f"  - [{ru['plan_name']}] {ru['rules']} (active {ru['start']} → {ru['end']})")
            rules_info = "Current active irrigation rules:\n" + "\n".join(rule_lines)

        system_prompt = f"""You are Krishimitra, an AI agricultural assistant for the AgroSense platform.
Your goal is to assist farmers with their agricultural needs, analyze sensor data, and provide crop-specific advice.

Farm details:
{farm_info}

{sensor_info}

{rules_info}

Current Time: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}

STRICT INSTRUCTIONS:
- You must strictly only answer questions related to agriculture, farming, crops, weather, and the farm details provided.
- If the user asks about anything unrelated to agriculture (like movies, programming, politics, general knowledge, etc.), politely decline and steer the conversation back to farming.
- Keep your answers concise, practical, and easy to understand.
- You can't perform actions like turning on/off the pump, changing the rules, etc. You can only provide information and suggestions.
- User then can use your suggestions to perform actions manually using these pages:(/periodic-irrigation, /sensor-triggered-irrigation).
- If user wants you to design the irrigation rules ask them to go to /auto-irrigation page
- Format your response in markdown formatting when necessary. Do not include large headers. Format links in [link](url).

EXAMPLES:
User: "What's the best time to water my crops?"
Krishimitra: "Based on your farm details and typical agricultural practices, the best time to water crops is early morning or late afternoon to minimize evaporation. Let me know if you need specific advice for your current crop!"

User: "Give me a recipe for chocolate cake"
Krishimitra: "I am Krishimitra, an agricultural assistant. I don't have information on recipes or cooking, but I can help you with crop management, soil health, or watering schedules. How can I assist with your farm today?"

User: "Write a python script"
Krishimitra: "I specialize in agriculture and farming guidance. I cannot help with programming or coding. Let me know if you need any assistance with your crops!"
"""

        # Build prompt string from history + current message for Ollama
        prompt_parts = []
        for msg in chat_history:
            role_label = "User" if msg["sender"] == "user" else "Krishimitra"
            prompt_parts.append(f"{role_label}: {msg['text']}")
        full_prompt = system_prompt + "\n\n" + "\n".join(prompt_parts) + "\nKrishimitra:"

        ollama_url = "http://localhost:11434/api/generate"

        def generate():
            payload = {
                "model": "deepseek-v3.2:cloud",
                "prompt": full_prompt,
                "stream": True
            }
            try:
                with requests.post(ollama_url, json=payload, stream=True, timeout=120) as resp:
                    for line in resp.iter_lines():
                        if not line:
                            continue
                        try:
                            chunk = json.loads(line.decode("utf-8"))
                        except Exception:
                            continue

                        token = chunk.get("response", "")
                        if token:
                            yield f"data: {json.dumps({'type': 'token', 'text': token})}\n\n"

                        if chunk.get("done"):
                            yield f"data: {json.dumps({'type': 'done'})}\n\n"
                            break
            except Exception as e:
                yield f"data: {json.dumps({'type': 'error', 'message': str(e)})}\n\n"

        return Response(
            stream_with_context(generate()),
            mimetype="text/event-stream",
            headers={
                "Cache-Control": "no-cache",
                "X-Accel-Buffering": "no"
            }
        )

    except Exception as e:
        return jsonify({"error": str(e)}), 500

@app.route("/api/chats", methods=["GET"])
def get_user_chats():
    if "userID" not in session:
        return jsonify({"error": "Unauthorized"}), 401
    
    chats = SQL_Get_ASSOC_DATA(f"SELECT id, chatData FROM chats WHERE userID={session['userID']}")
    result = {}
    if chats:
        for row in chats:
            try:
                result[row["id"]] = json.loads(row["chatData"])
            except:
                pass
    return jsonify(result)

@app.route("/api/chat/sync", methods=["POST"])
def sync_chat():
    if "userID" not in session:
        return jsonify({"error": "Unauthorized"}), 401
    
    data = request.get_json()
    chat_id = data.get("chat_id")
    chat_data = data.get("chat_data")
    
    if not chat_id or not chat_data:
        return jsonify({"error": "Missing parameters"}), 400
        
    chat_data_json = json.dumps(chat_data).replace("'", "''")
    
    SQL_SetData(f"INSERT OR REPLACE INTO chats (id, userID, chatData) VALUES ('{chat_id}', {session['userID']}, '{chat_data_json}')")
    
    return jsonify({"success": True})

@app.route("/api/chat/<chat_id>", methods=["DELETE"])
def delete_chat(chat_id):
    if "userID" not in session:
        return jsonify({"error": "Unauthorized"}), 401
        
    SQL_SetData(f"DELETE FROM chats WHERE id='{chat_id}' AND userID={session['userID']}")
    return jsonify({"success": True})

@app.route("/Get_ESPdata", methods=["GET"])
def GetESP_Entries(database="AgroSenseDB.db"):
    if "userID" not in session:
        return jsonify({"error": "Unauthorized"}), 401
    try:
        ESPid = SQL_Get_ASSOC_DATA(f"SELECT espID FROM userDetails WHERE id={session['userID']}")[0]["espID"]
    except:
        return jsonify({"error": "ESP ID not found"}), 500
    rows = SQL_Get_ASSOC_DATA(f"SELECT * FROM esp_logs WHERE espID='{ESPid}' ORDER BY entry_timestamp DESC",database=database)
    output = []
    if rows:
        try:
            latest_time = datetime.strptime(rows[0]["entry_timestamp"], "%Y-%m-%d %H:%M:%S")
        except:
            latest_time = datetime.now()
        for row in rows:
            try:
                row_time = datetime.strptime(row["entry_timestamp"], "%Y-%m-%d %H:%M:%S")
            except:
                continue
            if latest_time - row_time <= timedelta(hours=24):
                output.append({
                    "temp": row.get("temperature", 0),
                    "humid": row.get("humidity", 0),
                    "moisture": row.get("moisture", 0),
                    "water_level": row.get("water_level", 0),
                    "timestamp": row.get("entry_timestamp", "")
                })
            else:
                break
    try:
        ESP_Last_Scene = SQL_Get_ASSOC_DATA(f"SELECT lastActive FROM espDetails WHERE macADD='{ESPid}'")[0]["lastActive"]
    except:
        ESP_Last_Scene = None
    RelayLastStatus = SQL_Get_ASSOC_DATA(f"SELECT * FROM esp_logs WHERE espID='{ESPid}' ORDER BY entry_timestamp DESC LIMIT 1")
    RelayLastStatus = RelayLastStatus[0]["relay"] if RelayLastStatus else 0
    data = {
        "graph": output,
        "ESP_Last_Scene": ESP_Last_Scene,
        "RelayLastStatus": RelayLastStatus,
        "RecMoist": 50
    }

    return data


@app.route("/mark-notification-read", methods=["POST"])
def mark_notification_read():
    if "userID" not in session:
        return jsonify({"error": "Unauthorized"}), 401
    notification_id = request.form.get("notification_id")
    SQL_SetData(f"UPDATE notifications SET read=1 WHERE id={notification_id} AND userID={session['userID']}")
    return redirect("/dashboard")

@app.route("/profile")
def profile():
    if "userID" not in session:
        return redirect("/login")
    return render_template("profile.html")


@app.route("/profilePic", methods=["GET", "POST", "DELETE"])
def profile_pic():
    if "userID" not in session:
        return jsonify({"error": "Unauthorized"}), 401
    user_id = session["userID"]
    if request.method == "POST":
        if "file" not in request.files:
            return jsonify({"error": "No file provided"}), 400
        file = request.files["file"]
        if file.filename == "":
            return jsonify({"error": "Empty filename"}), 400
        if not file.filename.lower().endswith((".png", ".jpg", ".jpeg",".webp")):
            return jsonify({"error": "Invalid file type"}), 400
        image_bytes = file.read()
        SQL_SetData("UPDATE userDetails SET profilePic=? WHERE id=?",(image_bytes, user_id))
        return jsonify({"status": "uploaded"})
    elif request.method == "GET":
        data = SQL_Get_ASSOC_DATA("SELECT profilePic FROM userDetails WHERE id=?",(user_id,))
        image = data[0]["profilePic"]
        if image is None:
            return send_from_directory(os.path.join(os.path.dirname(__file__), "assets", "img"), "pfp.png")
        return Response(image, mimetype="image/png")
    elif request.method == "DELETE":
        SQL_SetData("UPDATE userDetails SET profilePic=NULL WHERE id=?",(user_id,))
        return jsonify({"status": "deleted"})
    
# AUTO-IRRIGATION AI ROUTE
@app.route("/api/auto-irrigate", methods=["POST"])
def auto_irrigate():
    if "userID" not in session:
        return jsonify({"error": "Unauthorized"}), 401
    user_id = session["userID"]
    # Fetch farm details
    farm = SQL_Get_ASSOC_DATA(f"SELECT * FROM farmDetails WHERE userID={user_id}")
    if not farm:
        return jsonify({"error": "Farm details not found"}), 404
    farm = farm[0]
    esp_id = farm["espID"]
    # Fetch recent esp_logs (last 24h)
    logs = SQL_Get_ASSOC_DATA(
        f"SELECT temperature, humidity, moisture, water_level, entry_timestamp "
        f"FROM esp_logs WHERE espID='{esp_id}' ORDER BY entry_timestamp DESC LIMIT 48"
    )
    logs_summary = "No sensor data available."
    if logs:
        temps = [l["temperature"] for l in logs if l["temperature"]]
        humids = [l["humidity"] for l in logs if l["humidity"]]
        moistures = [l["moisture"] for l in logs if l["moisture"]]
        avg_temp = round(sum(temps)/len(temps), 1) if temps else "N/A"
        avg_humid = round(sum(humids)/len(humids), 1) if humids else "N/A"
        avg_moist = round(sum(moistures)/len(moistures), 1) if moistures else "N/A"
        min_moist = round(min(moistures), 1) if moistures else "N/A"
        max_moist = round(max(moistures), 1) if moistures else "N/A"
        logs_summary = (
            f"Avg Temperature: {avg_temp}°C, Avg Humidity: {avg_humid}%, "
            f"Avg Soil Moisture: {avg_moist}% (Min: {min_moist}%, Max: {max_moist}%)"
        )

    # Fetch weather forecast
    weather_info = "No weather data available."
    try:
        API_KEY = "7b7df640cfdd61d7fbe4a12e1b06cf51"
        lat, lon = [x.strip() for x in farm["location"].split(",")]
        weather_resp = requests.get(
            f"https://api.openweathermap.org/data/2.5/forecast?lat={lat}&lon={lon}&appid={API_KEY}&units=metric",
            timeout=5
        )
        if weather_resp.status_code == 200:
            wdata = weather_resp.json()
            forecasts = wdata.get("list", [])[:8]  # next 24h (3h intervals)
            rain_expected = any(f.get("rain") for f in forecasts)
            forecast_temps = [f["main"]["temp"] for f in forecasts]
            forecast_desc = ", ".join(set(f["weather"][0]["description"] for f in forecasts[:4]))
            weather_info = (
                f"Rain expected in next 24h: {'Yes' if rain_expected else 'No'}. "
                f"Forecast temps: {min(forecast_temps):.1f}–{max(forecast_temps):.1f}°C. "
                f"Conditions: {forecast_desc}."
            )
    except Exception:
        pass

    now = datetime.now()
    now_str = now.strftime("%Y-%m-%d %H:%M:%S")
    end_10d = (now + timedelta(days=10)).strftime("%Y-%m-%d %H:%M:%S")

    system_prompt = f"""You are an expert irrigation planning AI for the AgroSense smart farming platform.
Your task is to generate a JSON array of irrigation rules for an ESP-based irrigation controller.

Farm Details:
- Soil Type: {farm['soilType']}
- Crop Type: {farm['cropType']}
- Location (lat,lon): {farm['location']}
- Total Area: {farm['totalArea']} acres

Sensor Stats (last 24h from ESP {esp_id}):
{logs_summary}

Weather Forecast:
{weather_info}

Current date/time: {now_str}
ESP MAC: {esp_id}

RULE FORMAT SPECIFICATION:
Each rule is a JSON object with these fields:
  id: (integer, sequential starting from 1)
  espID: (string, the ESP MAC address)
  ActiveStart: (string, datetime "YYYY-MM-DD HH:MM:SS", when this rule becomes active)
  ActiveEnd: (string, datetime "YYYY-MM-DD HH:MM:SS", when this rule expires)
  Rule: (string, JSON-stringified rule object — one of the types below)

Rule types:
1. periodic: {{"rule_name": "...", "rule_type": "periodic", "days": [0-6 list, 0=Sun...6=Sat], "start_time": "HHMMSS", "end_time": "HHMMSS"}}
2. moisture_based: {{"rule_name": "...", "rule_type": "moisture_based", "min": <int>, "max": <int>}}
3. rain_delay: {{"rule_type": "rain_delay"}} (use only if rain is forecast)

EXAMPLE OUTPUT 1 (Wheat farm, loamy soil, dry conditions, no rain):
[
  {{"id": 1, "espID": "AA:BB:CC:DD:EE:FF", "ActiveStart": "2026-03-28 00:00:00", "ActiveEnd": "2026-04-07 00:00:00", "Rule": "{{\\"rule_name\\": \\"Morning Watering\\", \\"rule_type\\": \\"periodic\\", \\"days\\": [0,1,2,3,4,5,6], \\"start_time\\": \\"060000\\", \\"end_time\\": \\"063000\\"}}"}},
  {{"id": 2, "espID": "AA:BB:CC:DD:EE:FF", "ActiveStart": "2026-03-28 00:00:00", "ActiveEnd": "2026-04-07 00:00:00", "Rule": "{{\\"rule_name\\": \\"Moisture Guard\\", \\"rule_type\\": \\"moisture_based\\", \\"min\\": 30, \\"max\\": 70}}"}}
]

EXAMPLE OUTPUT 2 (Rice paddy, clayey soil, high humidity, rain expected):
[
  {{"id": 1, "espID": "11:22:33:44:55:66", "ActiveStart": "2026-03-28 00:00:00", "ActiveEnd": "2026-04-07 00:00:00", "Rule": "{{\\"rule_name\\": \\"Rice Irrigation\\", \\"rule_type\\": \\"periodic\\", \\"days\\": [1,3,5], \\"start_time\\": \\"050000\\", \\"end_time\\": \\"054500\\"}}"}},
  {{"id": 2, "espID": "11:22:33:44:55:66", "ActiveStart": "2026-03-28 00:00:00", "ActiveEnd": "2026-04-07 00:00:00", "Rule": "{{\\"rule_name\\": \\"Soil Sensor Rule\\", \\"rule_type\\": \\"moisture_based\\", \\"min\\": 60, \\"max\\": 90}}"}},
  {{"id": 3, "espID": "11:22:33:44:55:66", "ActiveStart": "2026-03-28 00:00:00", "ActiveEnd": "2026-03-29 00:00:00", "Rule": "{{\\"rule_type\\": \\"rain_delay\\"}}"}}
]

EXAMPLE OUTPUT 3 (Vegetable garden, sandy soil, low moisture):
[
  {{"id": 1, "espID": "FF:EE:DD:CC:BB:AA", "ActiveStart": "2026-03-28 00:00:00", "ActiveEnd": "2026-04-07 00:00:00", "Rule": "{{\\"rule_name\\": \\"Evening Drip\\", \\"rule_type\\": \\"periodic\\", \\"days\\": [0,1,2,3,4,5,6], \\"start_time\\": \\"180000\\", \\"end_time\\": \\"183000\\"}}"}},
  {{"id": 2, "espID": "FF:EE:DD:CC:BB:AA", "ActiveStart": "2026-03-28 00:00:00", "ActiveEnd": "2026-04-07 00:00:00", "Rule": "{{\\"rule_name\\": \\"Sandy Soil Hydration\\", \\"rule_type\\": \\"moisture_based\\", \\"min\\": 20, \\"max\\": 55}}"}}
]

INSTRUCTIONS:
- Generate 2-4 rules tailored to the farm's soil type, crop, weather, and sensor readings.
- Think through the reasoning, then output ONLY a valid JSON array (no markdown, no extra text, no code fences).
- Use espID: "{esp_id}" for all rules.
- Set ActiveStart to "{now_str}" for all rules.
- Set ActiveEnd to "{end_10d}" unless it is a rain_delay (use next 24h for that).
- The Rule field must be a JSON-stringified string (double-quote escaped within the outer string).
"""

    def generate():
        """Stream thinking tokens via SSE, then save rules and send done event."""
        ollama_url = "http://localhost:11434/api/generate"
        payload = {
            "model": "deepseek-v3.2:cloud",
            "prompt": system_prompt,
            "stream": True,
            "think": True
        }

        full_response = ""
        thinking_text = ""

        try:
            with requests.post(ollama_url, json=payload, stream=True, timeout=120) as resp:
                for line in resp.iter_lines():
                    if not line:
                        continue
                    try:
                        chunk = json.loads(line.decode("utf-8"))
                    except Exception:
                        continue

                    # Stream thinking tokens
                    thinking = chunk.get("thinking", "")
                    if thinking:
                        thinking_text += thinking
                        sse = json.dumps({"type": "thinking", "text": thinking})
                        yield f"data: {sse}\n\n"

                    # Accumulate response
                    response_piece = chunk.get("response", "")
                    if response_piece:
                        full_response += response_piece

                    if chunk.get("done"):
                        break
        except Exception as e:
            yield f"data: {json.dumps({'type': 'error', 'message': str(e)})}\n\n"
            return

        # Parse the JSON rules from the model response
        rules = None
        try:
            # Strip markdown fences if present
            clean = re.sub(r"```[\w]*\n?", "", full_response).strip()
            # Find JSON array
            match = re.search(r"\[.*\]", clean, re.DOTALL)
            if match:
                rules = json.loads(match.group(0))
        except Exception as e:
            yield f"data: {json.dumps({'type': 'error', 'message': f'Failed to parse rules: {e}'})}\n\n"
            return

        if not rules:
            yield f"data: {json.dumps({'type': 'error', 'message': 'Model returned no valid rules.'})}\n\n"
            return

        # Delete existing rules for this ESP
        SQL_SetData(f"DELETE FROM irrigation_rules WHERE espID='{esp_id}'")

        # Insert new rules
        for rule in rules:
            rule_json = rule["Rule"] if isinstance(rule["Rule"], str) else json.dumps(rule["Rule"])
            rule_json_escaped = rule_json.replace("'", "''")
            SQL_SetData(
                f"INSERT INTO irrigation_rules (espID, ActiveStart, ActiveEnd, Rule) "
                f"VALUES ('{esp_id}', '{rule.get('ActiveStart', now_str)}', '{rule.get('ActiveEnd', end_10d)}', '{rule_json_escaped}')"
            )

        yield f"data: {json.dumps({'type': 'done', 'redirect': '/irrigation-rules'})}\n\n"

    return Response(
        stream_with_context(generate()),
        mimetype="text/event-stream",
        headers={
            "Cache-Control": "no-cache",
            "X-Accel-Buffering": "no"
        }
    )


# ESP ROUTES
@app.route("/heartBeat", methods=["POST"])
def receive_data():
    # try:
    data = request.get_json()
    if not data or "mac" not in data:
        print(data)
        return jsonify({"status": "error", "message": "Missing required field: mac"}), 400
    SQL_SetData(f"UPDATE espDetails SET lastActive=datetime('now', 'localtime'), battery='{data['batteryPercent']}' WHERE macADD='{data['mac']}'")

    prvDat = SQL_Get_ASSOC_DATA(f"SELECT temperature, humidity, relay, moisture, water_level, entry_timestamp FROM esp_logs WHERE espID='{data['mac']}' ORDER BY entry_timestamp DESC LIMIT 1")[0]
    update = False
    for key in data:
        if key=="mac" or key=="batteryPercent": continue
        if float(data[key]) != float(prvDat[key]):
            prvDat[key] = data[key]
            update = True
            print(f"Updated {key} to {data[key]}")
    if update:
        SQL_SetData(f"INSERT INTO esp_logs (espID, temperature, humidity, relay, moisture, water_level) VALUES ('{data['mac']}', {prvDat['temperature']}, {prvDat['humidity']}, {prvDat['relay']}, {prvDat['moisture']}, {prvDat['water_level']})")
    rules = SQL_Get_ASSOC_DATA(f"SELECT * FROM irrigation_rules WHERE espID='{data['mac']}' AND ActiveEnd > datetime('now', 'localtime')")
    if rules:
        return jsonify({
            "status": "success",
            "rules": rules
            }), 200
    else:
        return jsonify({
            "status": "success",
            "rules": []
            }), 200
if __name__ == "__main__":
    app.run(debug=True, host="0.0.0.0", port=82)

exit()
```

## database.py

```python
import sqlite3
import os
import base64
database="AgroSenseDB.db"

if not os.path.exists(database):
    with open("AgroSenseDB.db.sql", "r") as f:
        schema = f.read()
        f.close()
    SQL_DB = sqlite3.connect(database)
    SQL_CURSOR = SQL_DB.cursor()
    SQL_CURSOR.executescript(schema)
    SQL_DB.commit()
    SQL_DB.close()

def SQL_SetData(query, params=None, database=database):
    SQL_DB = sqlite3.connect(database)
    SQL_CURSOR = SQL_DB.cursor()
    if params:
        SQL_CURSOR.execute(query, params)
    else:
        SQL_CURSOR.execute(query)
    SQL_DB.commit()
    SQL_DB.close()

def SQL_Get_ASSOC_DATA(query, params=None, database=database):
    SQL_DB = sqlite3.connect(database)
    SQL_CURSOR = SQL_DB.cursor()
    if params:
        SQL_CURSOR.execute(query, params)
    else:
        SQL_CURSOR.execute(query)
    results = SQL_CURSOR.fetchall()
    results = [dict(zip([desc[0] for desc in SQL_CURSOR.description], row)) for row in results]
    SQL_DB.close()
    return results

def set_profile_pic(userId, image_source, database=database):
    SQL_DB = sqlite3.connect(database)
    SQL_CURSOR = SQL_DB.cursor()

    # Handle both file path and raw bytes
    if isinstance(image_source, str) and os.path.isfile(image_source):
        with open(image_source, "rb") as f:
            image_bytes = f.read()
    elif isinstance(image_source, (bytes, bytearray)):
        image_bytes = image_source
    else:
        raise ValueError("image_source must be a file path or bytes")

    SQL_CURSOR.execute(
        "UPDATE userDetails SET profilePic = ? WHERE id = ?",
        (sqlite3.Binary(image_bytes), userId)
    )
    SQL_DB.commit()
    SQL_DB.close()

def imageBytesToSrc(ImageBytes):    
    b64_str = base64.b64encode(ImageBytes).decode("utf-8")
    return f"data:image/png;base64,{b64_str}"


```

## AgroSenseDB.db.sql

```sql
BEGIN TRANSACTION;
CREATE TABLE IF NOT EXISTS "chats" (
	"id"	TEXT UNIQUE,
	"userID"	INTEGER,
	"chatData"	TEXT,
	PRIMARY KEY("id"),
	FOREIGN KEY("userID") REFERENCES "userDetails"("id")
);
CREATE TABLE IF NOT EXISTS "espDetails" (
	"macADD"	TEXT NOT NULL DEFAULT '3C:8A:1F:76:FB:C4' UNIQUE,
	"userID"	INTEGER NOT NULL UNIQUE,
	"lastActive"	DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),
	"battery"	INTEGER NOT NULL DEFAULT 0,
	PRIMARY KEY("macADD"),
	FOREIGN KEY("userID") REFERENCES "userDetails"("id")
);
CREATE TABLE IF NOT EXISTS "esp_logs" (
	"entry_id"	INTEGER,
	"espID"	TEXT NOT NULL DEFAULT '3C:8A:1F:76:FB:C4',
	"temperature"	FLOAT NOT NULL,
	"humidity"	FLOAT NOT NULL,
	"relay"	INTEGER NOT NULL DEFAULT 0,
	"moisture"	INTEGER,
	"water_level"	INTEGER,
	"entry_timestamp"	DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),
	PRIMARY KEY("entry_id" AUTOINCREMENT),
	FOREIGN KEY("espID") REFERENCES "espDetails"("macADD")
);
CREATE TABLE IF NOT EXISTS "farmDetails" (
	"id"	INTEGER,
	"userID"	INTEGER NOT NULL UNIQUE,
	"espID"	TEXT NOT NULL DEFAULT '3C:8A:1F:76:FB:C4' UNIQUE,
	"location"	TEXT NOT NULL,
	"totalArea"	INTEGER NOT NULL,
	"soilType"	TEXT NOT NULL,
	"cropType"	TEXT NOT NULL,
	PRIMARY KEY("id" AUTOINCREMENT),
	FOREIGN KEY("espID") REFERENCES "espDetails"("macADD"),
	FOREIGN KEY("userID") REFERENCES "userDetails"("id")
);
CREATE TABLE IF NOT EXISTS "irrigation_rules" (
	"id"	INTEGER NOT NULL,
	"espID"	TEXT NOT NULL DEFAULT '3C:8A:1F:76:FB:C4',
	"ActiveStart"	DATETIME NOT NULL DEFAULT (datetime('now', 'localtime')),
	"ActiveEnd"	DATETIME,
	"Rule"	TEXT NOT NULL,
	PRIMARY KEY("id" AUTOINCREMENT),
	FOREIGN KEY("espID") REFERENCES "espDetails"("macADD")
);
CREATE TABLE IF NOT EXISTS "notifications" (
	"id"	INTEGER NOT NULL,
	"userID"	INTEGER NOT NULL,
	"title"	TEXT NOT NULL,
	"description"	TEXT,
	"category"	TEXT NOT NULL DEFAULT 'info' CHECK("category" IN ('info', 'warn', 'success', 'danger')),
	"dateTime"	DATETIME DEFAULT (datetime('now', 'localtime')),
	"read"	INTEGER NOT NULL DEFAULT 0 CHECK("read" IN (0, 1)),
	PRIMARY KEY("id" AUTOINCREMENT),
	FOREIGN KEY("userID") REFERENCES "userDetails"("id")
);
CREATE TABLE IF NOT EXISTS "userDetails" (
	"id"	INTEGER NOT NULL,
	"userName"	TEXT NOT NULL,
	"emailId"	TEXT NOT NULL UNIQUE,
	"oAuth"	TEXT UNIQUE,
	"password"	TEXT NOT NULL,
	"espID"	TEXT DEFAULT '3C:8A:1F:76:FB:C4',
	"profilePic"	BLOB,
	PRIMARY KEY("id" AUTOINCREMENT),
	FOREIGN KEY("espID") REFERENCES "espDetails"("macADD")
);
INSERT INTO "chats" ("id","userID","chatData") VALUES ('1775246746641',1,'{"messages": [{"sender": "bot", "text": "Hello! I\u2019m Krishimitra \ud83c\udf31 How can I help?"}, {"sender": "user", "text": "hello there, how is it going?"}, {"sender": "bot", "text": "Hello! I\u2019m doing well, thank you. I see you have a small grass farm on red soil. Your recent sensor readings show stable temperature (28\u00b0C) and humidity (54%), but moisture is at 0% \u2014 which is unusual for grass. How can I assist you with your farm today?"}, {"sender": "user", "text": "are there any irrigation rules active right now?"}, {"sender": "bot", "text": "There are currently **no active irrigation rules** set for your farm.  \n\nYour moisture readings show 0% consistently, which is unusual for grass \u2014 you may want to check sensor placement or consider setting up an irrigation rule if the soil is indeed dry.  \nWould you like help creating one based on your crop type and soil conditions?"}, {"sender": "user", "text": "Yes"}, {"sender": "bot", "text": "Great. Let\u2019s set up a basic irrigation rule for your grass on red soil.\n\nSince red soil drains quickly and your moisture sensors show 0%, I\u2019d recommend:\n\n**Suggested Rule:**  \n- Start irrigation when soil moisture falls below **20%**.\n- Stop irrigation when moisture reaches **40%**.\n- Water preferably in the **early morning** (5\u20137 AM) to reduce evaporation losses.\n\nWould you like me to configure this rule for your farm on AgroSense, or do you want to adjust the thresholds first?  \n\nAlso, since moisture is reading 0%, double-check that your soil moisture sensor is properly placed in the root zone and functioning correctly."}, {"sender": "user", "text": "any rules active now?"}, {"sender": "bot", "text": "Currently, **no irrigation rules are active**.\n\nThe only listed rule is a *manual watering* scheduled for **Saturdays from 01:45 to 01:50**, which is not running right now (outside that time window).\n\nGiven your persistent 0% moisture readings, I strongly recommend:\n1.  Verifying your soil moisture sensor is properly installed and functional.\n2.  Setting up an automated rule (e.g., trigger irrigation when moisture < 20%) to maintain healthy grass.\n\nYou can create or adjust rules on the [Auto-Irrigation](/auto-irrigation) page."}, {"sender": "user", "text": "what pestiside should i use"}, {"sender": "bot", "text": "For grass on red soil, common pests include **armyworms, cutworms, chafer grubs, and fungal issues** like brown patch or dollar spot.  \n\n**General recommendations:**  \n- **For insects (grubs, caterpillars):** Use a broad-spectrum insecticide like **Chlorpyriphos** or **Lambda-cyhalothrin** (follow label directions).  \n- **For fungal diseases:** Apply **Carbendazim** or **Mancozeb** as a preventive spray.  \n- **Organic/natural alternatives:** Neem oil, garlic-chilli spray, or introducing beneficial nematodes for grubs.  \n\n**Important first steps:**  \n1. **Identify the pest/disease accurately** \u2014 check for chewed blades, brown patches, or larvae in soil.  \n2. **Improve grass health** \u2014 proper watering (avoid overwatering) and fertilization can reduce pest susceptibility.  \n3. **Spot-treat** affected areas before full-scale application.\n\nWould you like help identifying symptoms or adjusting irrigation to prevent fungal growth?"}, {"sender": "user", "text": "i  want to plant sugarcane  what fertilizers should i use and how should i irrigate?"}, {"sender": "bot", "text": "Excellent choice! Sugarcane is a major cash crop suitable for your region (Maharashtra) and red soil. Here\u2019s a concise guide:\n\n### \ud83c\udf31 **Fertilizer Recommendations for Sugarcane on Red Soil**\nRed soil is often low in organic matter, nitrogen, and phosphorus. A balanced fertilizer program is crucial.\n\n| Growth Stage | Fertilizer (per acre) | Key Notes |\n| :--- | :--- | :--- |\n| **Basal Application (at planting)** | 10\u201312 tonnes Farmyard Manure (FYM) or compost<br>**NPK:** 60:60:60 kg (N:P\u2082O\u2085:K\u2082O) | Incorporate well into soil to improve structure & water retention. |\n| **First Top-Dressing (30\u201345 days after planting)** | 30\u201340 kg Urea (for N) | Apply after first irrigation or rain. |\n| **Second Top-Dressing (90\u2013120 days)** | 30\u201340 kg Urea + 25 kg Potash (if needed) | Critical for tillering and stalk growth. |\n| **Micronutrients** | Zinc Sulphate (25 kg/ha) & Ferrous Sulphate (if yellowing appears) | Red soils can be deficient in zinc & iron. |\n\n**Organic Alternative:** Apply well-decomposed press mud (sugar factory waste) or green manure (like sunn hemp) before planting.\n\n### \ud83d\udca7 **Irrigation Schedule for Sugarcane**\nSugarcane needs ample but well-managed water. **Avoid waterlogging.**\n\n| Growth Phase | Irrigation Frequency (approx.) | Key Points |\n| :--- | :--- | :--- |\n| **Germination & Establishment (0\u201360 days)** | Light, frequent irrigations every 5\u20137 days | Maintain consistent soil moisture. |\n| **Tillering & Grand Growth (60\u2013180 days)** | Heavy irrigation every 10\u201312 days | Peak water demand period. |\n| **Ripening (180\u2013270 days)** | Reduce to every 15\u201320 days | Stress slightly to boost sugar accumulation. |\n\n**Method:** **Drip irrigation** is highly recommended for water efficiency and higher yield on red soil.\n\n### \ud83d\udccc **Immediate Steps for Your Farm**\n1.  **Test Soil:** Conduct a soil test for precise nutrient recommendations.\n2.  **Prepare Land:** Deep ploughing (30\u201345 cm) to break hardpans in red soil.\n3.  **Adjust Irrigation:** Since you\u2019re switching from grass, you\u2019ll need to **update your irrigation rules** on the [Auto-Irrigation](/auto-irrigation) page for sugarcane\u2019s needs.\n4.  **Monitor:** Use your moisture sensors to keep soil moisture between **60-80%** during active growth phases.\n\nWould you like help designing specific drip irrigation schedules or interpreting a soil test report?"}, {"sender": "user", "text": "how can i cook burger"}, {"sender": "bot", "text": "I am Krishimitra, an agricultural assistant. I don''t have information on cooking or recipes, but I can help you with your farm.\n\nYou were just asking about planting sugarcane on your red soil plot. Would you like to continue discussing fertilizer blends, irrigation setup, or any other aspect of sugarcane cultivation?"}, {"sender": "user", "text": "what is happening currently ??"}, {"sender": "bot", "text": "Based on your farm details and latest sensor data, here\u2019s what\u2019s happening **currently**:\n\n### \ud83d\udcca **Current Sensor Status (as of 12:04 PM)**\n- **Temperature:** 31.0\u00b0C  \n- **Humidity:** 43%  \n- **Soil Moisture:** 62%  \n- **Water Level:** Fluctuating (146 in latest reading)\n\n### \ud83c\udf3e **Farm Status**\n- **Current crop:** Grass (on 1 acre of red soil)  \n- **Irrigation rules:** None active  \n- **Soil moisture:** 62% is **adequate** for grass \u2014 no immediate irrigation needed.  \n- **Weather:** Warm and moderately dry (31\u00b0C, 43% humidity) \u2014 typical for early afternoon.\n\n### \ud83d\udd04 **Recent Discussion Context**\nYou\u2019ve shown interest in **switching from grass to sugarcane**.  \n- We discussed fertilizer and irrigation plans for sugarcane.  \n- Your current moisture readings are stable, but if you proceed with sugarcane, you\u2019ll need to **adjust irrigation rules** and **prepare the soil**.\n\n### \ud83d\ude9c **Suggested Immediate Actions**\n1. Verify moisture sensor placement (readings earlier showed 0% \u2014 now 62%).\n2. If switching to sugarcane, start **land preparation** and **soil testing**.\n3. Visit the [Auto-Irrigation](/auto-irrigation) page when ready to set sugarcane-specific rules.\n\nWould you like to proceed with sugarcane planning, or address something else on your farm?"}, {"sender": "user", "text": "which seeds should I prefer for wheets"}, {"sender": "bot", "text": "For wheat cultivation on your red soil (likely in Maharashtra), here are **recommended wheat varieties**:\n\n### \ud83c\udf3e **High-Yielding & Disease-Resistant Varieties for Maharashtra**\n| Variety | Key Features | Suitable For |\n| :--- | :--- | :--- |\n| **HD-3086** | High yield, resistant to rust & blight, medium duration (110-115 days) | Timely sown irrigated conditions |\n| **HD-2967** | High protein, resistant to brown rust, suitable for timely sowing | Irrigated plains of Peninsular India |\n| **NIAW-1415** | Developed for drought-prone areas, good for limited irrigation | Rainfed or scarce water regions |\n| **HI-1544** | Heat-tolerant, good for late sowing | Late planting situations |\n\n### \ud83d\udccc **Immediate Steps**\n1. **Confirm sowing time:** In Maharashtra, **timely sowing** (Nov \u2013 early Dec) is crucial for high yields.\n2. **Seed treatment:** Use fungicides like **Carbendazim** (2g/kg seed) to prevent soil-borne diseases.\n3. **Soil preparation:** Red soil needs good tilth \u2013 add 5\u201310 tonnes/acre of compost/FYM before planting.\n4. **Irrigation:** Wheat needs 4\u20136 irrigations; adjust rules on [Auto-Irrigation](/auto-irrigation) for moisture ~70-80% during vegetative growth.\n\n### \ud83d\udca1 **Pro Tip**\nFor red soil, consider **HD-3086** if you have assured irrigation, or **NIAW-1415** if water is limited. Always use **certified seeds** from reliable sources.\n\nWould you like help with wheat-specific fertilizer schedules or irrigation settings?"}], "title": "hello there, how is it going?"}');
INSERT INTO "espDetails" ("macADD","userID","lastActive","battery") VALUES ('3C:8A:1F:76:FB:C4',1,'2026-04-09 17:19:47',6);
INSERT INTO "esp_logs" ("entry_id","espID","temperature","humidity","relay","moisture","water_level","entry_timestamp") VALUES (12105,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,153,'2026-04-09 17:18:15'),
 (12106,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,150,'2026-04-09 17:18:17'),
 (12107,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,146,'2026-04-09 17:18:22'),
 (12108,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,150,'2026-04-09 17:18:23'),
 (12109,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,156,'2026-04-09 17:18:25'),
 (12110,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,153,'2026-04-09 17:18:27'),
 (12111,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,150,'2026-04-09 17:18:28'),
 (12112,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:18:33'),
 (12113,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,150,'2026-04-09 17:18:37'),
 (12114,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:18:38'),
 (12115,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,150,'2026-04-09 17:18:40'),
 (12116,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,146,'2026-04-09 17:18:44'),
 (12117,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,153,'2026-04-09 17:18:45'),
 (12118,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:18:47'),
 (12119,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,153,'2026-04-09 17:18:48'),
 (12120,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,156,'2026-04-09 17:18:50'),
 (12121,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,153,'2026-04-09 17:18:53'),
 (12122,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,153,'2026-04-09 17:18:54'),
 (12123,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,156,'2026-04-09 17:18:55'),
 (12124,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,153,'2026-04-09 17:18:57'),
 (12125,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:18:59'),
 (12126,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,156,'2026-04-09 17:19:01'),
 (12127,'3C:8A:1F:76:FB:C4',32.0,37.0,0,53,153,'2026-04-09 17:19:03'),
 (12128,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,153,'2026-04-09 17:19:04'),
 (12129,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:19:06'),
 (12130,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,146,'2026-04-09 17:19:22'),
 (12131,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:19:25'),
 (12132,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,156,'2026-04-09 17:19:26'),
 (12133,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,153,'2026-04-09 17:19:27'),
 (12134,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,146,'2026-04-09 17:19:28'),
 (12135,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:19:32'),
 (12136,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,140,'2026-04-09 17:19:35'),
 (12137,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:19:36'),
 (12138,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,143,'2026-04-09 17:19:38'),
 (12139,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,146,'2026-04-09 17:19:39'),
 (12140,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:19:41'),
 (12141,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,146,'2026-04-09 17:19:43'),
 (12142,'3C:8A:1F:76:FB:C4',32.0,37.0,0,52,150,'2026-04-09 17:19:44');
INSERT INTO "farmDetails" ("id","userID","espID","location","totalArea","soilType","cropType") VALUES (1,1,'3C:8A:1F:76:FB:C4','18.442001,73.8304973',1,'Red Soil','Grass');
INSERT INTO "irrigation_rules" ("id","espID","ActiveStart","ActiveEnd","Rule") VALUES (33,'3C:8A:1F:76:FB:C4','2026-04-04 10:40:00','2026-04-04 10:41:00','{"rule_name": "[WH]OneMinToday", "rule_type": "periodic", "days": [6], "start_time": "104000", "end_time": "104100"}'),
 (34,'3C:8A:1F:76:FB:C4','2026-04-04 10:44:34','2026-04-04 10:45:34','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [6], "start_time": "104434", "end_time": "104534"}'),
 (35,'3C:8A:1F:76:FB:C4','2026-04-04 10:47:48','2026-04-04 10:48:18','{"rule_name": "[WH]now_30sec", "rule_type": "periodic", "days": [6], "start_time": "104748", "end_time": "104818"}'),
 (36,'3C:8A:1F:76:FB:C4','2026-04-04 10:49:00','2026-04-04 10:51:00','{"rule_name": "[WH]now2min", "rule_type": "periodic", "days": [6], "start_time": "104900", "end_time": "105100"}'),
 (50,'3C:8A:1F:76:FB:C4','2026-04-08 12:04:03','2026-04-08 12:04:08','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [3], "start_time": "120403", "end_time": "120408"}'),
 (51,'3C:8A:1F:76:FB:C4','2026-04-08 12:02:14','2026-04-08 12:02:19','{"rule_name": "[WH]irrigate_now", "rule_type": "periodic", "days": [3], "start_time": "120214", "end_time": "120219"}'),
 (52,'3C:8A:1F:76:FB:C4','2026-04-08 12:24:22','2026-04-08 12:24:27','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [3], "start_time": "122422", "end_time": "122427"}'),
 (65,'3C:8A:1F:76:FB:C4','2026-04-09 15:51:38','2026-04-09 15:52:08','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [4], "start_time": "155138", "end_time": "155208"}'),
 (66,'3C:8A:1F:76:FB:C4','2026-04-09 15:54:16','2026-04-09 15:54:41','{"rule_name": "[WH]immediate", "rule_type": "periodic", "days": [4], "start_time": "155416", "end_time": "155441"}'),
 (67,'3C:8A:1F:76:FB:C4','2026-04-09 15:56:08','2026-04-09 15:56:38','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [4], "start_time": "155608", "end_time": "155638"}'),
 (68,'3C:8A:1F:76:FB:C4','2026-04-09 16:06:51','2026-04-09 16:07:06','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [4], "start_time": "160651", "end_time": "160706"}'),
 (69,'3C:8A:1F:76:FB:C4','2026-04-09 16:20:40','2026-04-09 16:21:10','{"rule_name": "Manual Watering", "rule_type": "periodic", "days": [4], "start_time": "162040", "end_time": "162110"}');
INSERT INTO "notifications" ("id","userID","title","description","category","dateTime","read") VALUES (1,1,'Low Soil Moisture','The soil moisture level is below the threshold in Field A.','warn','2026-01-16 10:41:00',1),
 (2,1,'New Weather Alert','A new weather alert has been issued for your area.','info','2026-01-16 10:36:00',1),
 (3,1,'Irrigation Completed','The irrigation cycle for Field B has been successfully completed.','success','2026-01-16 16:31:00',1),
 (4,1,'Pest Detected','Pests have been detected in Field C. Please take necessary actions.','danger','2026-01-16 16:26:00',1),
 (5,1,'Nutrient Deficiency','Nutrient deficiency detected in Crop D. Consider fertilization.','warn','2026-01-16 16:21:00',1);
INSERT INTO "userDetails" ("id","userName","emailId","oAuth","password","espID","profilePic") VALUES (1,'TestUser','test@test.test','','12345678','3C:8A:1F:76:FB:C4',NULL);
CREATE INDEX IF NOT EXISTS "ESPIDs" ON "esp_logs" (
	"espID"	ASC
);
COMMIT;

```

## whatsapp_Service.py

```python
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
WHATSAPP_TO = "918689976202@s.whatsapp.net"
ESP_ID = "3C:8A:1F:76:FB:C4"

def get_whatsapp_messages():
    command = [
        "./wacli",
        "--store", "./wacliSync",
        "messages", "list",
        "--chat", "143649643974860@lid",
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
```

