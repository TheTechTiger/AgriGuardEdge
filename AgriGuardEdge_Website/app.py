from flask import jsonify, redirect, request, send_from_directory, render_template, session, Flask,Response,send_file, stream_with_context
import os, json, re, time
from database import *
from datetime import datetime,timedelta
import requests

# flask run --debug -h 0.0.0.0 -p 80
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
# NOTE: Flask(__name__) (not a plain string) so template/static paths are
# anchored at this file's directory — the server must boot identically
# whether launched from here, the repo root, or anywhere else.
app = Flask(__name__)
app.secret_key = os.environ.get("SECRET_KEY", ".--secret-key--..")

env_path = os.path.join(BASE_DIR, ".env")
if os.path.exists(env_path):
    with open(env_path) as f:
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
        API_KEY = os.environ.get("OWM_API_KEY")
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
        # Logged-out pages (login-adjacent, Contact, FAQ) still extend
        # base.html, which dereferences profile_data — hand it a guest.
        return dict(profile_data={"userName": "Guest", "emailId": "",
                                  "profilePic": "assets/img/pfp.png"},
                    notifications=[])
    profile_data = SQL_Get_ASSOC_DATA(f"SELECT userName, emailId, profilePic FROM userDetails WHERE id={session.get('userID', -1)}")[0]
    profile_data["profilePic"] = imageBytesToSrc(profile_data["profilePic"]) if profile_data["profilePic"] else "assets/img/pfp.png"
    notifications = SQL_Get_ASSOC_DATA(f"SELECT * FROM notifications WHERE userID={session.get('userID', -1)} ORDER BY dateTime LIMIT 5")
    return dict(profile_data=profile_data, notifications=notifications)
    
@app.route("/assets/<path:filename>")
def serve_static(filename):
    return send_from_directory(os.path.join(BASE_DIR, "assets"), filename)

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

        system_prompt = f"""You are AgriGuard, an AI agricultural assistant for the AgriGuard Edge platform.
Your goal is to assist farmers with their agricultural needs, analyze sensor data, and explain edge-detected irrigation anomalies (leaks, blockages, sensor faults, water stress).

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
AgriGuard: "Based on your farm details and typical agricultural practices, the best time to water crops is early morning or late afternoon to minimize evaporation. Let me know if you need specific advice for your current crop!"

User: "Give me a recipe for chocolate cake"
AgriGuard: "I am AgriGuard, an agricultural assistant. I don't have information on recipes or cooking, but I can help you with crop management, soil health, or watering schedules. How can I assist with your farm today?"

User: "Write a python script"
AgriGuard: "I specialize in agriculture and farming guidance. I cannot help with programming or coding. Let me know if you need any assistance with your crops!"
"""

        # Build prompt string from history + current message for Ollama
        prompt_parts = []
        for msg in chat_history:
            role_label = "User" if msg["sender"] == "user" else "AgriGuard"
            prompt_parts.append(f"{role_label}: {msg['text']}")
        full_prompt = system_prompt + "\n\n" + "\n".join(prompt_parts) + "\nAgriGuard:"

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
def GetESP_Entries(database=None):
    database = database or os.path.join(BASE_DIR, "AgriGuardEdgeDB.db")
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
        API_KEY = os.environ.get("OWM_API_KEY")
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

    system_prompt = f"""You are an expert irrigation planning AI for the AgriGuard Edge platform.
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


# AGUARD ROUTES — edge anomaly ingest + dashboard data (arch §8/§9)
# Device sends decisions, not data: kind = alert | summary | probe_result |
# heartbeat. Server stamps received_at (no NTP per wake, §6.8).
@app.route("/api/ingest", methods=["POST"])
def api_ingest():
    data = request.get_json(silent=True) or {}
    msg_id = data.get("msg_id")
    device_id = data.get("device_id", "esp32_01")
    kind = data.get("kind", "heartbeat")
    if not msg_id:
        return jsonify({"status": "error", "message": "Missing msg_id"}), 400
    now = time.time()
    already = SQL_Get_ASSOC_DATA(f"SELECT msg_id FROM messages WHERE msg_id='{msg_id}'")
    if already:
        return jsonify({"status": "success", "msg_id": msg_id, "duplicate": True}), 200
    payload = json.dumps(data).replace("'", "''")
    SQL_SetData(
        "INSERT OR IGNORE INTO messages (msg_id, received_at, device_id, kind, payload) "
        f"VALUES ('{msg_id}', {now}, '{device_id}', '{kind}', '{payload}')")
    if kind in ("alert", "probe_result"):
        ev = data.get("event", "NONE")
        sev = data.get("severity", "INFO")
        state = data.get("evidence", {}).get("state", "")
        evidence = json.dumps(data.get("evidence", {})).replace("'", "''")
        SQL_SetData(
            "INSERT INTO events (device_id, received_at, event, severity, state, evidence) "
            f"VALUES ('{device_id}', {now}, '{ev}', '{sev}', '{state}', '{evidence}')")
    if kind == "probe_result":
        evd = data.get("evidence", {})
        SQL_SetData(
            "INSERT INTO probe_results (received_at, device_id, verdict, V_delivered_ml, "
            "V_soil_delta_ml, residual_ratio, ratio_vs_reference, flow_calibrated_ppl) "
            f"VALUES ({now}, '{device_id}', '{data.get('event', 'NONE')}', "
            f"{evd.get('V_delivered_ml', 0)}, {evd.get('V_soil_delta_ml', 0)}, "
            f"{evd.get('residual_ratio', 0)}, {evd.get('ratio_vs_reference', 1)}, "
            f"{evd.get('flow_calibrated_ppl', 5888)})")
    if kind == "summary":
        w = data.get("window", {})
        e = data.get("energy", {})
        SQL_SetData(
            "INSERT INTO summaries (device_id, received_at, avg_moisture, min_moisture, "
            "max_moisture, std_moisture, avg_temp, avg_rh, vbat, soc_pct, mAh) "
            f"VALUES ('{device_id}', {now}, {w.get('avg_moisture', 0)}, {w.get('min', 0)}, "
            f"{w.get('max', 0)}, {w.get('std', 0)}, {w.get('avg_temp', 0)}, {w.get('avg_rh', 0)}, "
            f"{e.get('vbat', 0)}, {e.get('soc_pct', 0)}, {e.get('mah_today', 0)})")
        sv = data.get("savings", {})
        SQL_SetData(
            "INSERT INTO energy_log (received_at, device_id, tx_count, tx_bytes, awake_s, probe_cycles) "
            f"VALUES ({now}, '{device_id}', {sv.get('messages_sent', 0)}, 0, 0, 0)")
    return jsonify({"status": "success", "msg_id": msg_id}), 200

@app.route("/api/events", methods=["GET"])
def api_events():
    device = request.args.get("device_id", "esp32_01")
    rows = SQL_Get_ASSOC_DATA(
        f"SELECT * FROM events WHERE device_id='{device}' ORDER BY received_at DESC LIMIT 50")
    for r in rows:
        try:
            r["evidence"] = json.loads(r["evidence"]) if r["evidence"] else {}
        except Exception:
            pass
    return jsonify(rows)

@app.route("/api/summary", methods=["GET"])
def api_summary():
    device = request.args.get("device_id", "esp32_01")
    rows = SQL_Get_ASSOC_DATA(
        f"SELECT * FROM summaries WHERE device_id='{device}' ORDER BY received_at DESC LIMIT 96")
    return jsonify(rows)

@app.route("/api/energy", methods=["GET"])
def api_energy():
    device = request.args.get("device_id", "esp32_01")
    rows = SQL_Get_ASSOC_DATA(
        f"SELECT * FROM energy_log WHERE device_id='{device}' ORDER BY received_at DESC LIMIT 96")
    return jsonify(rows)

@app.route("/api/probes", methods=["GET"])
def api_probes():
    device = request.args.get("device_id", "esp32_01")
    rows = SQL_Get_ASSOC_DATA(
        f"SELECT * FROM probe_results WHERE device_id='{device}' ORDER BY received_at DESC LIMIT 50")
    return jsonify(rows)

@app.route("/api/ack", methods=["POST"])
def api_ack():
    if "userID" not in session:
        return jsonify({"error": "Unauthorized"}), 401
    data = request.get_json(silent=True) or {}
    ev_id = int(data.get("id", -1))
    SQL_SetData(f"UPDATE events SET ack_state='ACK' WHERE id={ev_id}")
    return jsonify({"success": True})

@app.route("/api/scenario", methods=["POST"])
def api_scenario():
    # Demo scenario launcher (§11): records an operator-injected scenario as
    # an INFO event so the dashboard timeline shows what was done physically.
    if "userID" not in session:
        return jsonify({"error": "Unauthorized"}), 401
    data = request.get_json(silent=True) or {}
    device = data.get("device_id", "esp32_01")
    name = data.get("scenario", "baseline").replace("'", "''")
    now = time.time()
    SQL_SetData(
        "INSERT INTO events (device_id, received_at, event, severity, state, evidence) "
        f"VALUES ('{device}', {now}, 'EXTERNAL_EVENT', 'INFO', 'SCENARIO', '{{\'scenario\': \'{name}\'}}')")
    return jsonify({"success": True, "scenario": data.get("scenario")})

@app.route("/health", methods=["GET"])
def health():
    return jsonify({"status": "ok", "service": "AgriGuardEdge"}), 200

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