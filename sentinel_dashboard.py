#!/usr/bin/env python3
"""SENTINEL Raspberry Pi dashboard: Arduino status, USB webcam, device inventory."""
import csv
import io
import ipaddress
import logging
import os
import re
import sqlite3
import threading
import time
from datetime import datetime, timezone

import cv2
import serial
from flask import Flask, Response, jsonify, render_template_string, request, send_file

BASE_DIR = "/home/pi/sentinel"
SERIAL_PORT = "/dev/ttyACM0"
BAUD_RATE = 115200
CAMERA_DEVICE = "/dev/video0"
HOST, PORT = "0.0.0.0", 5000
LOG_FILE = os.path.join(BASE_DIR, "sentinel_dashboard.log")
DB_FILE = os.path.join(BASE_DIR, "sentinel_devices.db")
os.makedirs(BASE_DIR, exist_ok=True)

logging.basicConfig(filename=LOG_FILE, level=logging.INFO,
                    format="%(asctime)s %(levelname)s %(message)s")
app = Flask(__name__)
state_lock = threading.Lock()
serial_lock = threading.Lock()
camera_lock = threading.Lock()
connect_lock = threading.Lock()
arduino = None
camera = None

robot_status = {
    "connected": False, "security": "UNKNOWN", "robot_mode": "UNKNOWN",
    "movement": "UNKNOWN", "security_state": "UNKNOWN",
    "last_update": "Not yet checked", "last_error": None,
}
camera_status = {"connected": False, "device": CAMERA_DEVICE, "resolution": "640 x 480"}


def now_utc():
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def db():
    conn = sqlite3.connect(DB_FILE, timeout=10)
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    with db() as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS devices (
              id INTEGER PRIMARY KEY AUTOINCREMENT,
              name TEXT NOT NULL, device_type TEXT NOT NULL DEFAULT 'Unknown',
              hostname TEXT NOT NULL DEFAULT '', ip_address TEXT NOT NULL DEFAULT '',
              mac_address TEXT NOT NULL DEFAULT '', owner_label TEXT NOT NULL DEFAULT '',
              notes TEXT NOT NULL DEFAULT '', enabled INTEGER NOT NULL DEFAULT 1,
              created_at TEXT NOT NULL, updated_at TEXT NOT NULL
            )
        """)
    logging.info("Device inventory database initialized")
    sync_owner_devices()


def sync_owner_devices():
    """Copy owner_devices.db records into the dashboard inventory idempotently."""
    owner_db = os.path.join(BASE_DIR, "owner_devices.db")
    if not os.path.exists(owner_db):
        logging.info("Owner registry not found; skipping owner-device sync")
        return
    try:
        with sqlite3.connect(owner_db, timeout=10) as source:
            source.row_factory = sqlite3.Row
            tables = {r[0] for r in source.execute(
                "SELECT name FROM sqlite_master WHERE type='table'").fetchall()}
            if "trusted_devices" not in tables:
                logging.warning("owner_devices.db has no trusted_devices table")
                return
            cols = {r[1] for r in source.execute("PRAGMA table_info(trusted_devices)").fetchall()}
            if not {"device_name", "device_type"}.issubset(cols):
                logging.warning("Owner registry schema differs; skipping sync")
                return
            rows = [dict(r) for r in source.execute("SELECT * FROM trusted_devices").fetchall()]
        with db() as target:
            for r in rows:
                name = str(r.get("device_name") or "").strip()
                if not name:
                    continue
                values = (name, str(r.get("device_type") or "Unknown"),
                          str(r.get("hostname") or ""), str(r.get("ip_address") or ""),
                          str(r.get("mac_address") or "").upper().replace("-", ":"),
                          str(r.get("owner_name") or ""), "Imported from owner_devices.db",
                          int(bool(r.get("enabled", 1))))
                existing = target.execute("SELECT id FROM devices WHERE name=? COLLATE NOCASE", (name,)).fetchone()
                now = now_utc()
                if existing:
                    target.execute("""UPDATE devices SET device_type=?,
                        hostname=CASE WHEN ?<>'' THEN ? ELSE hostname END,
                        ip_address=CASE WHEN ?<>'' THEN ? ELSE ip_address END,
                        mac_address=CASE WHEN ?<>'' THEN ? ELSE mac_address END,
                        owner_label=CASE WHEN ?<>'' THEN ? ELSE owner_label END,
                        enabled=?, updated_at=? WHERE id=?""",
                        (values[1], values[2], values[2], values[3], values[3],
                         values[4], values[4], values[5], values[5], values[7], now, existing["id"]))
                else:
                    target.execute("""INSERT INTO devices
                        (name,device_type,hostname,ip_address,mac_address,owner_label,notes,enabled,created_at,updated_at)
                        VALUES (?,?,?,?,?,?,?,?,?,?)""", values[:7] + (values[7], now, now))
        logging.info("Synced %d owner device record(s) to dashboard inventory", len(rows))
    except (sqlite3.Error, OSError) as exc:
        logging.exception("Could not sync owner device registry: %s", exc)


def validate_device(data):
    name = str(data.get("name", "")).strip()
    if not name:
        raise ValueError("Device name is required.")
    if len(name) > 100:
        raise ValueError("Device name must be 100 characters or fewer.")
    result = {
        "name": name,
        "device_type": str(data.get("device_type", "Unknown")).strip()[:60] or "Unknown",
        "hostname": str(data.get("hostname", "")).strip()[:253],
        "ip_address": str(data.get("ip_address", "")).strip(),
        "mac_address": str(data.get("mac_address", "")).strip().upper(),
        "owner_label": str(data.get("owner_label", "")).strip()[:100],
        "notes": str(data.get("notes", "")).strip()[:1000],
    }
    if result["ip_address"]:
        try:
            ipaddress.ip_address(result["ip_address"])
        except ValueError:
            raise ValueError("Enter a valid IPv4 or IPv6 address.")
    mac = result["mac_address"]
    if mac and not re.fullmatch(r"(?:[0-9A-F]{2}[:-]){5}[0-9A-F]{2}", mac):
        raise ValueError("MAC address must look like AA:BB:CC:DD:EE:FF.")
    result["mac_address"] = mac.replace("-", ":")
    return result


def list_devices():
    with db() as conn:
        rows = conn.execute("SELECT * FROM devices ORDER BY enabled DESC, name COLLATE NOCASE").fetchall()
    return [dict(row) for row in rows]


def connect_arduino():
    global arduino
    with connect_lock:
        if arduino is not None and arduino.is_open:
            return True
        try:
            port = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=0.25, write_timeout=2)
            logging.info("Serial opened; waiting for Arduino startup")
            time.sleep(4)  # USB serial may reset the Arduino
            port.reset_input_buffer()
            arduino = port
            with state_lock:
                robot_status["connected"] = True
                robot_status["last_error"] = None
            logging.info("Arduino serial connection opened")
            return True
        except Exception as exc:
            arduino = None
            with state_lock:
                robot_status["connected"] = False
                robot_status["last_error"] = str(exc)
            logging.error("Arduino connection failed: %s", exc)
            return False


def arduino_command(command, wait_seconds=1.5):
    """Allow only status and the two supported security commands; no movement commands."""
    global arduino
    if command not in {"STATUS", "SECURITY,LOCKDOWN", "SECURITY,NORMAL"}:
        logging.warning("Rejected unsupported dashboard serial command")
        return []
    if arduino is None or not arduino.is_open:
        if not connect_arduino():
            return []
    answers = []
    with serial_lock:
        try:
            arduino.write((command + "\n").encode("ascii"))
            arduino.flush()
            logging.info("Arduino command sent: %s", command)
            deadline = time.monotonic() + wait_seconds
            while time.monotonic() < deadline:
                raw = arduino.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                answers.append(line)
                if line.startswith("EVENT,"):
                    logging.warning("ARDUINO_SECURITY_EVENT %s", line)
                else:
                    logging.info("Arduino response: %s", line)
                if command == "STATUS" and line.startswith("STATUS,"):
                    break
                if command != "STATUS" and line.startswith(("OK,", "ERR,")):
                    break
            for line in answers:
                if not line.startswith("STATUS,"):
                    continue
                fields = {}
                for item in line.split(",")[1:]:
                    if "=" in item:
                        k, v = item.split("=", 1)
                        fields[k] = v
                with state_lock:
                    robot_status.update({
                        "connected": True,
                        "security": fields.get("SECURITY", "UNKNOWN"),
                        "robot_mode": fields.get("ROBOT", "UNKNOWN"),
                        "movement": fields.get("MOVEMENT", "UNKNOWN"),
                        "security_state": fields.get("STATE", "UNKNOWN"),
                        "last_update": time.strftime("%Y-%m-%d %H:%M:%S"),
                        "last_error": None,
                    })
            return answers
        except Exception as exc:
            logging.exception("Arduino serial communication error")
            with state_lock:
                robot_status["connected"] = False
                robot_status["last_error"] = str(exc)
            try:
                arduino.close()
            except Exception:
                pass
            arduino = None
            return []


def open_camera():
    global camera
    with camera_lock:
        if camera is not None and camera.isOpened():
            return True
        try:
            candidate = cv2.VideoCapture(CAMERA_DEVICE, cv2.CAP_V4L2)
            candidate.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
            candidate.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
            if not candidate.isOpened():
                candidate.release()
                camera_status["connected"] = False
                logging.error("Could not open USB webcam")
                return False
            ok, frame = candidate.read()
            if not ok or frame is None:
                candidate.release()
                camera_status["connected"] = False
                logging.error("Could not capture webcam frame")
                return False
            camera = candidate
            camera_status["connected"] = True
            logging.info("USB webcam opened successfully")
            return True
        except Exception as exc:
            camera_status["connected"] = False
            logging.exception("Camera error: %s", exc)
            return False


def generate_frames():
    while True:
        if not open_camera():
            time.sleep(2)
            continue
        with camera_lock:
            if camera is None:
                continue
            ok, frame = camera.read()
        if not ok or frame is None:
            camera_status["connected"] = False
            time.sleep(0.2)
            continue
        ok, jpg = cv2.imencode(".jpg", frame, [int(cv2.IMWRITE_JPEG_QUALITY), 75])
        if not ok:
            continue
        camera_status["connected"] = True
        yield b"--frame\r\nContent-Type: image/jpeg\r\n\r\n" + jpg.tobytes() + b"\r\n"


PAGE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SENTINEL Security Dashboard</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#07111f;color:#e5edf7;font-family:Arial,sans-serif}
header{padding:22px;background:#0d1b2d;border-bottom:1px solid #24374d}h1{margin:0;letter-spacing:5px}
header p,.note{color:#93a9c4;line-height:1.5}.container{max-width:1300px;margin:auto;padding:20px}
.grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:14px}
.card{background:#101e30;border:1px solid #263b52;border-radius:12px;padding:18px;margin-bottom:16px;min-width:0}
.label{color:#91a7c1;font-size:12px;text-transform:uppercase;letter-spacing:1px}.value{margin-top:10px;font-size:21px;font-weight:bold;overflow-wrap:anywhere}
.green{color:#4ade80}.yellow{color:#facc15}.red{color:#fb7185}.blue{color:#60a5fa}
button{background:#1d4ed8;color:white;border:0;border-radius:7px;padding:10px 14px;cursor:pointer;margin:4px}
button.danger{background:#991b1b}button.secondary{background:#475569}
input,select,textarea{width:100%;padding:10px;border:1px solid #39516d;border-radius:7px;background:#081526;color:#e5edf7;margin:5px 0 12px}
label{display:block;color:#b8cce4;font-size:13px}.camera{width:100%;max-height:650px;object-fit:contain;background:#02060b;border-radius:8px}
pre{white-space:pre-wrap;overflow-wrap:anywhere;color:#b8cce4;max-height:280px;overflow:auto}
.table-wrap{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:13px}th,td{text-align:left;padding:10px;border-bottom:1px solid #2a4058;vertical-align:top;overflow-wrap:anywhere}
th{color:#93a9c4}.badge{display:inline-block;padding:4px 8px;border-radius:12px;background:#174b36;color:#86efac}.off{background:#4b2028;color:#fecdd3}
.form-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}footer{text-align:center;padding:24px;color:#7188a4}
@media(max-width:800px){.grid,.form-grid{grid-template-columns:repeat(2,minmax(0,1fr))}}@media(max-width:450px){.grid,.form-grid{grid-template-columns:1fr}.container{padding:10px}}
</style></head><body>
<header><h1>SENTINEL</h1><p>ROBOT SECURITY AND MONITORING CENTER</p></header>
<main class="container">
<section class="grid">
<div class="card"><div class="label">Arduino Connection</div><div class="value" id="connection">CHECKING</div></div>
<div class="card"><div class="label">Security Mode</div><div class="value" id="security">UNKNOWN</div></div>
<div class="card"><div class="label">Robot Mode</div><div class="value" id="mode">UNKNOWN</div></div>
<div class="card"><div class="label">Movement</div><div class="value" id="movement">UNKNOWN</div></div>
</section>
<section class="card"><h2>Live USB Camera</h2><img class="camera" src="/video_feed" alt="SENTINEL live camera feed">
<p class="note">USB webcam streamed by Raspberry Pi at up to 640 × 480.</p><div class="value" id="cameraStatus">CAMERA STATUS: CHECKING</div></section>
<section class="card"><h2>Robot and Security Status</h2>
<p>Security state: <strong id="securityState">UNKNOWN</strong></p><p>Last status update: <span id="lastUpdate">Not yet checked</span></p>
<p class="note" id="error"></p><button onclick="refreshStatus()">Refresh Status</button>
<button class="danger" onclick="sendSecurity('lockdown')">Request Lockdown</button>
<button class="secondary" onclick="sendSecurity('normal')">Request Normal Mode</button>
<p class="note">Buttons send only the supported security serial commands. Keep motor power disconnected during testing. This dashboard does not independently detect attacks.</p></section>
<section class="card"><h2>Registered Device Inventory</h2>
<p class="note">This is a manually maintained registry, not automatic network discovery. IP/MAC addresses alone do not authenticate a device.</p>
<form id="deviceForm"><div class="form-grid">
<div><label>Device name *<input name="name" required maxlength="100" placeholder="Owner laptop"></label></div>
<div><label>Device type<select name="device_type"><option>Owner computer</option><option>Owner phone</option><option>Raspberry Pi</option><option>Arduino</option><option>Camera</option><option>Network device</option><option>Unknown</option><option>Other</option></select></label></div>
<div><label>Hostname<input name="hostname" maxlength="253" placeholder="laptop.local"></label></div>
<div><label>IP address<input name="ip_address" placeholder="192.168.1.25"></label></div>
<div><label>MAC address<input name="mac_address" placeholder="AA:BB:CC:DD:EE:FF"></label></div>
<div><label>Owner / authorization label<input name="owner_label" maxlength="100" placeholder="Robot owner"></label></div>
</div><label>Notes<textarea name="notes" rows="2" maxlength="1000" placeholder="How this device was verified"></textarea></label>
<button type="submit">Register Device</button><span id="deviceMessage" class="note" role="status"></span></form>
<div class="table-wrap"><table><thead><tr><th>Name / Type</th><th>Hostname</th><th>IP</th><th>MAC</th><th>Owner / Notes</th><th>Status</th><th>Actions</th></tr></thead>
<tbody id="deviceRows"><tr><td colspan="7">Loading inventory…</td></tr></tbody></table></div>
<button onclick="refreshDevices()">Refresh Devices</button><button class="secondary" onclick="location.href='/api/devices/export.csv'">Export CSV</button>
<p class="note">Enable/disable affects only this inventory record; it does not block network access or authenticate a device.</p></section>
<section class="card"><h2>Dashboard Event Log</h2><button onclick="refreshLogs()">Refresh Logs</button><pre id="logs">Loading log…</pre></section>
<section class="card"><h2>Security Reports</h2><p class="note">CSV export currently covers the device inventory. Incident report generation and intrusion-detection rules are not implemented.</p></section>
</main><footer>SENTINEL · Raspberry Pi Security Dashboard</footer>
<script>
function color(el,v){el.className='value';if(['NORMAL','SECURE','CONNECTED'].includes(v))el.classList.add('green');else if(['ALERT','UNKNOWN','CHECKING'].includes(v))el.classList.add('yellow');else if(['LOCKDOWN','ATTACK','DISCONNECTED','DASHBOARD ERROR'].includes(v))el.classList.add('red');else el.classList.add('blue')}
async function refreshStatus(){try{let r=await fetch('/api/status',{cache:'no-store'}),d=await r.json();let c=document.getElementById('connection');c.innerText=d.robot.connected?'CONNECTED':'DISCONNECTED';color(c,c.innerText);let s=document.getElementById('security');s.innerText=d.robot.security;color(s,s.innerText);document.getElementById('mode').innerText=d.robot.robot_mode;document.getElementById('movement').innerText=d.robot.movement;document.getElementById('securityState').innerText=d.robot.security_state;document.getElementById('lastUpdate').innerText=d.robot.last_update;document.getElementById('error').innerText=d.robot.last_error||'';let cam=document.getElementById('cameraStatus');cam.innerText=d.camera.connected?'CAMERA STATUS: CONNECTED':'CAMERA STATUS: DISCONNECTED';color(cam,d.camera.connected?'CONNECTED':'DISCONNECTED')}catch(e){let c=document.getElementById('connection');c.innerText='DASHBOARD ERROR';color(c,c.innerText)}}
async function refreshLogs(){try{let r=await fetch('/api/logs',{cache:'no-store'}),d=await r.json();document.getElementById('logs').textContent=d.lines.join('\n')||'No events recorded yet.'}catch(e){document.getElementById('logs').textContent='Could not load log.'}}
async function refreshDevices(){let tb=document.getElementById('deviceRows');try{let r=await fetch('/api/devices',{cache:'no-store'}),d=await r.json();tb.replaceChildren();if(!d.devices.length){let row=tb.insertRow(),c=row.insertCell();c.colSpan=7;c.textContent='No devices registered yet.';return}for(const x of d.devices){let row=tb.insertRow(),cell=v=>{let c=row.insertCell();c.textContent=v||'—';return c};cell(x.name+' / '+x.device_type);cell(x.hostname);cell(x.ip_address);cell(x.mac_address);cell((x.owner_label||'No owner label')+(x.notes?'\n'+x.notes:''));let sc=cell(''),b=document.createElement('span');b.className=x.enabled?'badge':'badge off';b.textContent=x.enabled?'ENABLED':'DISABLED';sc.appendChild(b);let ac=cell(''),t=document.createElement('button');t.className='secondary';t.textContent=x.enabled?'Disable':'Enable';t.onclick=()=>deviceAction(x.id,x.enabled?'disable':'enable');ac.appendChild(t);let del=document.createElement('button');del.className='danger';del.textContent='Delete';del.onclick=()=>{if(confirm('Delete record for '+x.name+'?'))deviceAction(x.id,'delete')};ac.appendChild(del)}}catch(e){tb.replaceChildren();let row=tb.insertRow(),c=row.insertCell();c.colSpan=7;c.textContent='Could not load inventory.'}}
async function deviceAction(id,action){try{let r=await fetch('/api/devices/'+id,{method:action==='delete'?'DELETE':'PATCH',headers:{'Content-Type':'application/json'},body:action==='delete'?undefined:JSON.stringify({enabled:action==='enable'})}),d=await r.json();if(!r.ok)throw Error(d.error||'Request failed');refreshDevices()}catch(e){alert(e.message)}}
document.getElementById('deviceForm').addEventListener('submit',async e=>{e.preventDefault();let f=e.currentTarget,p=Object.fromEntries(new FormData(f).entries()),m=document.getElementById('deviceMessage');try{let r=await fetch('/api/devices',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)}),d=await r.json();if(!r.ok)throw Error(d.error||'Registration failed');f.reset();m.textContent='Device registered.';refreshDevices()}catch(err){m.textContent=err.message}});
async function sendSecurity(mode){if(!confirm(mode==='lockdown'?'Request lockdown? Keep motor battery disconnected during testing.':'Request normal mode? Ensure robot is physically safe.'))return;try{let r=await fetch('/api/security/'+mode,{method:'POST'}),d=await r.json();alert((d.responses||[]).join('\n')||d.error||'No response');refreshStatus();refreshLogs()}catch(e){alert('Could not send security command.')}}
refreshStatus();refreshLogs();refreshDevices();setInterval(refreshStatus,5000);setInterval(refreshLogs,10000);setInterval(refreshDevices,15000);
</script></body></html>"""


@app.route("/")
def index():
    logging.info("Dashboard page requested")
    return render_template_string(PAGE)


@app.route("/video_feed")
def video_feed():
    return Response(generate_frames(), mimetype="multipart/x-mixed-replace; boundary=frame")


@app.route("/api/status")
def api_status():
    responses = arduino_command("STATUS")
    with state_lock:
        robot = dict(robot_status)
    if not any(x.startswith("STATUS,") for x in responses):
        robot["connected"] = False
    return jsonify(robot=robot, camera=dict(camera_status))


@app.route("/api/logs")
def api_logs():
    try:
        with open(LOG_FILE, encoding="utf-8") as f:
            lines = f.readlines()[-100:]
    except OSError:
        lines = []
    return jsonify(lines=[x.rstrip() for x in lines])


@app.route("/api/devices", methods=["GET", "POST"])
def api_devices():
    if request.method == "GET":
        return jsonify(devices=list_devices())
    data = request.get_json(silent=True)
    if not isinstance(data, dict):
        return jsonify(error="Expected a JSON object."), 400
    try:
        item = validate_device(data)
        now = now_utc()
        with db() as conn:
            cur = conn.execute("""INSERT INTO devices
                (name,device_type,hostname,ip_address,mac_address,owner_label,notes,enabled,created_at,updated_at)
                VALUES (?,?,?,?,?,?,?,1,?,?)""",
                (item["name"], item["device_type"], item["hostname"], item["ip_address"],
                 item["mac_address"], item["owner_label"], item["notes"], now, now))
            device_id = cur.lastrowid
        logging.info("Device inventory record created: id=%s", device_id)
        return jsonify(ok=True, id=device_id), 201
    except ValueError as exc:
        return jsonify(error=str(exc)), 400
    except sqlite3.Error:
        logging.exception("Device database error")
        return jsonify(error="Could not save device record."), 500


@app.route("/api/devices/<int:device_id>", methods=["PATCH", "DELETE"])
def api_device_item(device_id):
    with db() as conn:
        if request.method == "DELETE":
            cur = conn.execute("DELETE FROM devices WHERE id=?", (device_id,))
        else:
            data = request.get_json(silent=True)
            if not isinstance(data, dict) or not isinstance(data.get("enabled"), bool):
                return jsonify(error="Provide enabled as true or false."), 400
            cur = conn.execute("UPDATE devices SET enabled=?,updated_at=? WHERE id=?",
                               (int(data["enabled"]), now_utc(), device_id))
        conn.commit()
    if cur.rowcount == 0:
        return jsonify(error="Device record not found."), 404
    logging.info("Device inventory record changed: id=%s", device_id)
    return jsonify(ok=True)


@app.route("/api/devices/export.csv")
def export_devices():
    output = io.StringIO()
    fields = ["id", "name", "device_type", "hostname", "ip_address", "mac_address",
              "owner_label", "notes", "enabled", "created_at", "updated_at"]
    writer = csv.DictWriter(output, fieldnames=fields)
    writer.writeheader()
    for item in list_devices():
        writer.writerow({k: item.get(k, "") for k in fields})
    memory = io.BytesIO(output.getvalue().encode("utf-8-sig"))
    memory.seek(0)
    return send_file(memory, mimetype="text/csv", as_attachment=True,
                     download_name="sentinel_device_inventory.csv")


@app.route("/api/security/<mode>", methods=["POST"])
def api_security(mode):
    commands = {"lockdown": "SECURITY,LOCKDOWN", "normal": "SECURITY,NORMAL"}
    command = commands.get(mode.lower())
    if command is None:
        return jsonify(ok=False, error="Unsupported security mode."), 400
    responses = arduino_command(command, wait_seconds=2.0)
    if not responses:
        return jsonify(ok=False, error="No Arduino response; check USB connection.", responses=[]), 503
    success = any(x.startswith("OK,") for x in responses)
    return jsonify(ok=success, responses=responses), (200 if success else 409)


if __name__ == "__main__":
    init_db()
    print("=" * 55)
    print(" SENTINEL SECURITY DASHBOARD")
    print("=" * 55)
    print("Camera device:", CAMERA_DEVICE)
    print("Serial device:", SERIAL_PORT)
    print("Device database:", DB_FILE)
    print("Dashboard port:", PORT)
    print("Arduino serial port opened." if connect_arduino() else "WARNING: Arduino serial connection failed.")
    print("USB webcam opened successfully." if open_camera() else "WARNING: USB webcam could not be opened.")
    print("\nDashboard starting...")
    print("Open the Raspberry Pi IP address on port 5000.")
    print("Press Ctrl+C to stop the dashboard.")
    print("WARNING: No login/authentication. Use only on a trusted private network.")
    try:
        app.run(host=HOST, port=PORT, debug=False, threaded=True, use_reloader=False)
    finally:
        if camera is not None:
            camera.release()
        if arduino is not None and arduino.is_open:
            arduino.close()
        logging.info("Dashboard stopped")
