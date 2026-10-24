"""Local Wi-Fi radar dashboard for the Guardian Arduino project."""

import argparse
import json
import re
import socket
import threading
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial
from serial.tools import list_ports


BAUD_RATE = 9600
WEB_PORT = 8000
RADAR_PATTERN = re.compile(
    r"Radar: (\d+) deg \| Distance: (no echo|[0-9]+(?:\.[0-9]+)?) ?(?:cm)?"
    r" \| Target: (YES|NO) \| Zone: (OCCUPIED|CLEAR|OFF)"
)

state_lock = threading.Lock()
state = {
    "connected": False,
    "distance": None,
    "angle": None,
    "points": {},
    "zone": "WAITING",
    "lastAlert": "Waiting for Guardian data...",
    "updatedAt": None,
}


def update_state(**changes):
    with state_lock:
        state.update(changes)


def state_as_json():
    with state_lock:
        return json.dumps(state).encode("utf-8")


def process_line(line):
    match = RADAR_PATTERN.search(line)
    if match:
        angle_text, distance_text, target_text, zone = match.groups()
        angle = int(angle_text)
        distance = None if distance_text == "no echo" else float(distance_text)
        with state_lock:
            state["distance"] = distance
            state["angle"] = angle
            state["zone"] = zone
            state["points"][str(angle)] = {
                "distance": distance,
                "target": target_text == "YES",
            }
            state["updatedAt"] = datetime.now().strftime("%I:%M:%S %p")
        return

    if "Keep radar area empty" in line:
        update_state(points={}, lastAlert="Calibrating the empty area...")
    elif "ALERT: Zone occupied" in line:
        update_state(lastAlert="Alert: protected zone occupied")
    elif "CLEAR: Zone is empty" in line:
        update_state(lastAlert="Clear: protected zone is empty")
    elif "SYSTEM DISARMED" in line:
        update_state(lastAlert="System disarmed by remote")
    elif "SYSTEM OFF" in line:
        update_state(zone="OFF", lastAlert="System turned off by remote")
    elif "SYSTEM ARMED" in line:
        update_state(lastAlert="System armed by remote")
    elif "SYSTEM ON" in line:
        update_state(lastAlert="System turned on by remote")
    elif "SYSTEM RESET" in line:
        update_state(points={}, lastAlert="System reset: calibrating radar...")
    elif "SOUND MUTED" in line:
        update_state(lastAlert="Sound muted: monitoring continues")


def read_guardian(port_name):
    while True:
        try:
            print(f"Connecting to Guardian on {port_name}...")
            with serial.Serial(port_name, BAUD_RATE, timeout=1) as guardian:
                guardian.reset_input_buffer()
                update_state(connected=True, lastAlert="Connected. Guardian is calibrating...")
                print("Connected to Guardian.")
                while True:
                    line = guardian.readline().decode("utf-8", errors="replace").strip()
                    if line:
                        print(line)
                        process_line(line)
        except serial.SerialException as error:
            update_state(connected=False, lastAlert=f"Serial connection lost: {error}")
            print(f"Serial connection unavailable: {error}")
            time.sleep(2)


PAGE = """<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Guardian Radar</title>
  <style>
    :root { color-scheme: dark; font-family: system-ui, sans-serif; }
    body { margin: 0; min-height: 100vh; display: grid; place-items: center; background: #101827; }
    main { width: min(94vw, 440px); text-align: center; padding: 18px 0; }
    h1 { margin: 0 0 16px; font-size: 1.7rem; }
    .card { border: 2px solid #42c77b; border-radius: 24px; background: #1d2a3d; padding: 18px; box-shadow: 0 16px 42px #0008; }
    .label { color: #b7c6dc; font-weight: 700; letter-spacing: .08em; font-size: .72rem; }
    #zone { margin: 6px 0 10px; font-size: 1.8rem; font-weight: 800; color: #42c77b; }
    #radar { width: 100%; max-width: 390px; border-radius: 14px; background: #07130f; }
    .readings { display: flex; justify-content: center; gap: 38px; margin: 14px 0; }
    #distance, #angle { font-size: 1.8rem; font-weight: 800; line-height: 1.1; }
    .unit { color: #b7c6dc; font-size: .86rem; }
    #message { min-height: 1.4em; color: #d6e2f5; }
    #updated { margin-top: 12px; color: #97a9c1; font-size: .78rem; }
    .occupied { border-color: #ff4d5d; } .occupied #zone { color: #ff4d5d; }
    .disarmed { border-color: #ffbd4a; } .disarmed #zone { color: #ffbd4a; }
    .waiting, .disconnected, .off { border-color: #7891b5; } .waiting #zone, .disconnected #zone, .off #zone { color: #b7c6dc; }
  </style>
</head>
<body>
  <main>
    <h1>Guardian Radar</h1>
    <section class="card waiting" id="card">
      <div class="label">PROTECTED ZONE</div>
      <div id="zone">WAITING</div>
      <canvas id="radar" width="390" height="270"></canvas>
      <div class="readings">
        <div><div class="label">DISTANCE</div><div id="distance">--</div><div class="unit">cm</div></div>
        <div><div class="label">SCAN ANGLE</div><div id="angle">--</div><div class="unit">degrees</div></div>
      </div>
      <div id="message">Waiting for Guardian data...</div>
      <div id="updated">Not connected yet</div>
    </section>
  </main>
  <script>
    const canvas = document.querySelector('#radar');
    const context = canvas.getContext('2d');
    const maxDistanceCm = 250;

    function pointFor(angle, distance, radius, centerX, centerY) {
      const radians = Math.PI + angle * Math.PI / 180;
      const scaledRadius = Math.min(distance / maxDistanceCm, 1) * radius;
      return {x: centerX + Math.cos(radians) * scaledRadius,
              y: centerY + Math.sin(radians) * scaledRadius};
    }

    function drawRadar(data) {
      const width = canvas.width;
      const height = canvas.height;
      const centerX = width / 2;
      const centerY = height - 18;
      const radius = height - 36;
      context.clearRect(0, 0, width, height);
      context.fillStyle = '#07130f';
      context.fillRect(0, 0, width, height);
      context.strokeStyle = '#1c7653';
      context.lineWidth = 1;
      for (let ring = 1; ring <= 4; ring++) {
        context.beginPath();
        context.arc(centerX, centerY, radius * ring / 4, Math.PI, 2 * Math.PI);
        context.stroke();
      }
      [20, 90, 160].forEach(angle => {
        const edge = pointFor(angle, maxDistanceCm, radius, centerX, centerY);
        context.beginPath();
        context.moveTo(centerX, centerY);
        context.lineTo(edge.x, edge.y);
        context.stroke();
      });
      context.fillStyle = '#99d9bc';
      context.font = '11px system-ui';
      context.fillText('LEFT', 8, height - 5);
      context.fillText('FRONT', centerX - 18, 15);
      context.fillText('RIGHT', width - 42, height - 5);

      Object.entries(data.points || {}).forEach(([angle, reading]) => {
        if (reading.distance === null) return;
        const point = pointFor(Number(angle), reading.distance, radius, centerX, centerY);
        context.fillStyle = reading.target ? '#ff4d5d' : '#49c7e8';
        context.beginPath();
        context.arc(point.x, point.y, reading.target ? 6 : 3, 0, 2 * Math.PI);
        context.fill();
      });
      if (data.angle !== null) {
        const scanEnd = pointFor(data.angle, maxDistanceCm, radius, centerX, centerY);
        context.strokeStyle = '#91f8a8';
        context.lineWidth = 2;
        context.beginPath();
        context.moveTo(centerX, centerY);
        context.lineTo(scanEnd.x, scanEnd.y);
        context.stroke();
      }
      context.fillStyle = '#ffffff';
      context.beginPath();
      context.arc(centerX, centerY, 5, 0, 2 * Math.PI);
      context.fill();
    }

    async function refresh() {
      try {
        const data = await fetch('/api/status', {cache: 'no-store'}).then(response => response.json());
        const zone = data.connected ? data.zone : 'DISCONNECTED';
        document.querySelector('#zone').textContent = zone;
        document.querySelector('#distance').textContent = data.distance === null ? '--' : data.distance.toFixed(1);
        document.querySelector('#angle').textContent = data.angle === null ? '--' : data.angle + '°';
        document.querySelector('#message').textContent = data.lastAlert;
        document.querySelector('#updated').textContent = data.updatedAt ? 'Last scan: ' + data.updatedAt : 'No scan yet';
        document.querySelector('#card').className = 'card ' + zone.toLowerCase();
        drawRadar(data);
      } catch (error) {
        document.querySelector('#zone').textContent = 'OFFLINE';
        document.querySelector('#message').textContent = 'Dashboard connection lost.';
      }
    }
    drawRadar({points: {}, angle: null});
    refresh();
    setInterval(refresh, 200);
  </script>
</body>
</html>"""


class DashboardHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/api/status":
            body = state_as_json()
            content_type = "application/json"
        elif self.path == "/":
            body = PAGE.encode("utf-8")
            content_type = "text/html; charset=utf-8"
        else:
            self.send_error(404, "Page not found")
            return
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format_string, *arguments):
        return


def find_lan_ip():
    connection = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        connection.connect(("8.8.8.8", 80))
        return connection.getsockname()[0]
    except OSError:
        return "your-laptop-ip"
    finally:
        connection.close()


def main():
    parser = argparse.ArgumentParser(description="Show Guardian radar on a phone browser.")
    parser.add_argument("port", nargs="?", help="Arduino serial port, for example COM3")
    parser.add_argument("--list-ports", action="store_true", help="Show available serial ports.")
    arguments = parser.parse_args()
    if arguments.list_ports:
        for port in list_ports.comports():
            print(f"{port.device} - {port.description}")
        return
    if not arguments.port:
        parser.error("Enter the Arduino port, for example: python guardian_phone_dashboard.py COM3")

    threading.Thread(target=read_guardian, args=(arguments.port,), daemon=True).start()
    lan_ip = find_lan_ip()
    print(f"Open on this laptop: http://localhost:{WEB_PORT}")
    print(f"Open on your phone: http://{lan_ip}:{WEB_PORT}")
    print("Your phone and laptop must use the same Wi-Fi. Press Ctrl+C to stop.")
    ThreadingHTTPServer(("0.0.0.0", WEB_PORT), DashboardHandler).serve_forever()


if __name__ == "__main__":
    main()
