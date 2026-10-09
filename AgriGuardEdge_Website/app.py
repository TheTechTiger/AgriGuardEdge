from flask import jsonify, redirect, request, send_from_directory, render_template, session, Flask, Response
import os, json, time
from database import *

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
    return render_template("dashboard.html")

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
            return send_from_directory(os.path.join(BASE_DIR, "assets", "img"), "pfp.png")
        return Response(image, mimetype="image/png")
    elif request.method == "DELETE":
        SQL_SetData("UPDATE userDetails SET profilePic=NULL WHERE id=?",(user_id,))
        return jsonify({"status": "deleted"})

# AGUARD ROUTES — edge anomaly ingest + dashboard data (arch §8/§9)
# Device sends decisions, not data: kind = alert | summary | probe_result |
# heartbeat. Server stamps received_at (no NTP per wake, §6.8).
def _q(s):
    return str(s).replace("'", "''")


def _store_ingest(device, kind, data, now, msg_id):
    # Single writer for device-shaped payloads (§8): messages + derived
    # events / probe_results / summaries / energy_log rows.
    payload = json.dumps(data).replace("'", "''")
    SQL_SetData(
        "INSERT OR IGNORE INTO messages (msg_id, received_at, device_id, kind, payload) "
        f"VALUES ('{_q(msg_id)}', {now}, '{_q(device)}', '{kind}', '{payload}')")
    if kind in ("alert", "probe_result"):
        ev = data.get("event", "NONE")
        sev = data.get("severity", "INFO")
        state = data.get("evidence", {}).get("state", "")
        evidence = json.dumps(data.get("evidence", {})).replace("'", "''")
        SQL_SetData(
            "INSERT INTO events (device_id, received_at, event, severity, state, evidence) "
            f"VALUES ('{_q(device)}', {now}, '{_q(ev)}', '{_q(sev)}', '{_q(state)}', '{evidence}')")
    if kind == "probe_result":
        evd = data.get("evidence", {})
        SQL_SetData(
            "INSERT INTO probe_results (received_at, device_id, verdict, V_delivered_ml, "
            "V_soil_delta_ml, residual_ratio, ratio_vs_reference, flow_calibrated_ppl) "
            f"VALUES ({now}, '{_q(device)}', '{_q(data.get('event', 'NONE'))}', "
            f"{evd.get('V_delivered_ml', 0)}, {evd.get('V_soil_delta_ml', 0)}, "
            f"{evd.get('residual_ratio', 0)}, {evd.get('ratio_vs_reference', 1)}, "
            f"{evd.get('flow_calibrated_ppl', 450)})")
    if kind == "summary":
        w = data.get("window", {})
        e = data.get("energy", {})
        SQL_SetData(
            "INSERT INTO summaries (device_id, received_at, avg_moisture, min_moisture, "
            "max_moisture, std_moisture, avg_temp, avg_rh, vbat, soc_pct, mAh) "
            f"VALUES ('{_q(device)}', {now}, {w.get('avg_moisture', 0)}, {w.get('min', 0)}, "
            f"{w.get('max', 0)}, {w.get('std', 0)}, {w.get('avg_temp', 0)}, {w.get('avg_rh', 0)}, "
            f"{e.get('vbat', 0)}, {e.get('soc_pct', 0)}, {e.get('mah_today', 0)})")
        sv = data.get("savings", {})
        SQL_SetData(
            "INSERT INTO energy_log (received_at, device_id, tx_count, tx_bytes, awake_s, probe_cycles) "
            f"VALUES ({now}, '{_q(device)}', {sv.get('messages_sent', 0)}, 0, 0, 0)")


@app.route("/api/ingest", methods=["POST"])
def api_ingest():
    data = request.get_json(silent=True) or {}
    msg_id = data.get("msg_id")
    device_id = data.get("device_id", "esp32_01")
    kind = data.get("kind", "heartbeat")
    if not msg_id:
        return jsonify({"status": "error", "message": "Missing msg_id"}), 400
    already = SQL_Get_ASSOC_DATA(f"SELECT msg_id FROM messages WHERE msg_id='{_q(msg_id)}'")
    if already:
        return jsonify({"status": "success", "msg_id": msg_id, "duplicate": True}), 200
    _store_ingest(device_id, kind, data, time.time(), msg_id)
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

@app.route("/health", methods=["GET"])
def health():
    return jsonify({"status": "ok", "service": "AgriGuardEdge"}), 200

@app.route("/api/status", methods=["GET"])
def api_status():
    # Replaces the legacy ESP last-seen: newest ingest across all tables.
    # Window is 75 min: calm nodes send summaries hourly (15-min cadence,
    # adaptive ÷4), so 15 min would cry wolf on a healthy sleeping node.
    rows = SQL_Get_ASSOC_DATA("SELECT MAX(received_at) AS last_seen, COUNT(*) AS n FROM messages")
    last = rows[0]["last_seen"] if rows and rows[0]["last_seen"] else 0
    return jsonify({"last_seen": last,
                    "online": (time.time() - last) < 4500 if last else False,
                    "messages": rows[0]["n"] if rows else 0}), 200

# WS HUB — realtime pump test. ESP links as a websocket CLIENT (NAT-free
# on the LAN); browsers link as UI. Server relays pump UI→ESP, flow
# ESP→UI, presence both ways, and pings ESPs every 10 s (their dead-man
# switch). Port 8765, all interfaces. Local-demo trust model: no auth.
import asyncio as _asyncio
import threading as _threading
WS_PORT = 8765
_hub = {"esps": {}, "uis": set()}


async def _ws_send(ws, obj):
    try:
        await ws.send(json.dumps(obj))
    except Exception:
        pass


async def _ws_bcast_ui(obj):
    for u in list(_hub["uis"]):
        await _ws_send(u, obj)


async def _ws_handler(ws, *a):
    role, device = None, "esp32_01"
    try:
        async for raw in ws:
            try:
                m = json.loads(raw)
            except Exception:
                continue
            if m.get("role") == "esp":
                device = str(m.get("device", device))
                role = "esp"
                _hub["esps"][device] = ws
                await _ws_bcast_ui({"type": "esp", "device": device, "online": True})
                continue
            if m.get("role") == "ui":
                role = "ui"
                _hub["uis"].add(ws)
                for d in _hub["esps"]:
                    await _ws_send(ws, {"type": "esp", "device": d, "online": True})
                continue
            t = m.get("type")
            if t == "pump" and role == "ui":
                w = _hub["esps"].get(str(m.get("device", device)))
                if w:
                    await _ws_send(w, {"type": "pump", "on": bool(m.get("on"))})
            elif t == "flow" and role == "esp":
                d = str(m.get("device", device))
                for u in list(_hub["uis"]):
                    await _ws_send(u, {"type": "flow", "device": d,
                                       "t": m.get("t"), "flow_lpm": m.get("flow_lpm"),
                                       "vol_ml": m.get("vol_ml"), "pump": m.get("pump")})
    finally:
        if role == "esp":
            if _hub["esps"].get(device) is ws:
                del _hub["esps"][device]
            await _ws_bcast_ui({"type": "esp", "device": device, "online": False})
        if role == "ui":
            _hub["uis"].discard(ws)


async def _ws_ping():
    while True:
        await _asyncio.sleep(10)
        for w in list(_hub["esps"].values()):
            await _ws_send(w, {"type": "ping"})


async def _ws_main():
    from websockets.asyncio.server import serve
    async with serve(_ws_handler, "0.0.0.0", WS_PORT):
        await _ws_ping()


def _ws_start():
    try:
        import websockets  # noqa: F401
    except ImportError:
        print("[ws] websockets lib missing — realtime hub disabled")
        return

    def run():
        loop = _asyncio.new_event_loop()
        _asyncio.set_event_loop(loop)
        loop.run_until_complete(_ws_main())

    _threading.Thread(target=run, daemon=True).start()
    print(f"[ws] hub on 0.0.0.0:{WS_PORT}", flush=True)


if __name__ == "__main__":
    _ws_start()  # realtime pump-test hub (:8765); no-op if lib missing
    app.run(debug=True, host="0.0.0.0", port=82)

exit()