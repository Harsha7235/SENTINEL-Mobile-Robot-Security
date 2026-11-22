SENTINEL — Mobile Robot Security Monitoring & Lockdown Framework
A Raspberry Pi 5 + Arduino UNO R4 WiFi security-monitoring prototype for a four-wheel mobile robot.

SENTINEL combines a physical mobile robot with a separate monitoring and security layer. The Arduino UNO R4 WiFi handles robot-side control and controller-enforced lockdown. A Raspberry Pi 5 runs a Python/Flask dashboard, communicates with the Arduino over USB serial, streams a USB webcam through OpenCV, records events, and maintains a local SQLite device inventory.
Prototype status: This repository contains the source listings reproduced in the SENTINEL project report. The documented prototype demonstrated the dashboard, live USB camera feed, manual device registration, Pi-to-Arduino serial status, and a tested serial LOCKDOWN response. In one tested serial path, MOVE,FORWARD was rejected with ERR,LOCKDOWN. Other control paths and broader security claims remain unverified.

Repository name
SENTINEL-Mobile-Robot-Security
GitHub description
Raspberry Pi 5 + Arduino UNO R4 WiFi mobile robot security prototype featuring a Flask monitoring dashboard, USB camera streaming, serial event logging, device inventory, and controller-enforced lockdown.
Project goals
- Make the robot's operational and security state visible.
- Show a live USB webcam feed in a browser dashboard.
- Maintain a local inventory of known devices.
- Record timestamped commands and controller responses.
- Enforce movement rejection in the Arduino firmware while LOCKDOWN is active.
- Provide a foundation for future authentication, alerts, and repeatable security evaluation.
Hardware
Component	Role / documented configuration
Raspberry Pi 5 (8 GB reported)	Runs Raspberry Pi OS 64-bit, Flask dashboard, camera capture, serial controller, logs, and SQLite inventory.
Arduino UNO R4 WiFi	Robot-side controller, serial protocol, Wi-Fi control interface, movement/security logic.
Four-wheel chassis	Four DC motors grouped as left-side and right-side motor pairs.
L298N dual H-bridge	Drives the left and right motor groups.
USB webcam	OpenCV capture; /dev/video0 was tested at 640×480.
HC-SR04 ultrasonic sensor	TRIG D8, ECHO D9.
Camera / scan servos	D6 and D7.
I²C LCD	Address 0x27.
Power bank	5 V supply for Raspberry Pi during reported tests.


Documented Arduino pin assignments
Function	Arduino pin
L298N IN1	D2
L298N IN2	D3
L298N IN3	D4
L298N IN4	D5
Camera servo	D6
Ultrasonic scan servo	D7
HC-SR04 TRIG	D8
HC-SR04 ECHO	D9
LCD	I²C, address 0x27


The four motors are grouped by side in the documented design. Check the actual motor-driver wiring and motor polarity on your chassis before powering it.
System architecture
Laptop / operator browser
          |
          | HTTP dashboard
          v
Raspberry Pi 5
  ├── Flask web dashboard
  ├── OpenCV USB webcam stream (/dev/video0)
  ├── PySerial controller (115200 baud)
  ├── Timestamped event log
  └── SQLite device inventory + CSV export
          |
          | USB serial, 115200 baud
          v
Arduino UNO R4 WiFi
  ├── Robot movement logic
  ├── NORMAL / LOCKDOWN security state
  ├── HC-SR04 + servo scan logic
  ├── Wi-Fi control page
  └── Status / command responses
          |
          v
L298N motor driver
          |
          v
Four DC motors / four-wheel chassis
Security design principle: movement restrictions must be enforced in the Arduino/controller, not only by hiding or disabling dashboard buttons.
Software stack
- Arduino C++ firmware using WiFiS3, Servo, Wire, and LiquidCrystal_I2C.
- Python 3 on Raspberry Pi OS.
- Flask for the browser dashboard and HTTP endpoints.
- OpenCV for USB webcam capture and MJPEG streaming.
- PySerial for USB serial communication.
- SQLite for the manually maintained device inventory.
- Python logging and CSV export.
Repository layout
SENTINEL-Mobile-Robot-Security/
├── README.md
├── firmware/
│   └── sentinel_arduino.ino
├── raspberry_pi/
│   ├── sentinel_dashboard.py
│   └── requirements.txt
├── docs/
│   └── SOURCE_CODE_ORIGIN.md
└── .gitignore
Getting started
1. Arduino firmware
1. Open firmware/sentinel_arduino.ino in Arduino IDE.
2. Select Arduino UNO R4 WiFi as the board.
3. Install the libraries required by the sketch:
   - Servo
   - LiquidCrystal_I2C
   - WiFiS3 and Wire are supplied with the relevant Arduino board core.
4. Replace the redacted Wi-Fi password and owner-control token placeholders with your own values before deployment.
5. Verify pin assignments against your physical wiring.
6. Upload the sketch and test first with the motor battery disconnected.
2. Raspberry Pi dashboard
On Raspberry Pi OS:
mkdir -p ~/sentinel
cd ~/sentinel
Copy raspberry_pi/sentinel_dashboard.py into this directory, then install dependencies:
sudo apt update
sudo apt install -y python3-pip python3-opencv python3-venv
python3 -m venv --system-site-packages .venv
source .venv/bin/activate
pip install -r requirements.txt
If using a virtual environment without system OpenCV, install a compatible OpenCV wheel in that environment instead.

Check serial and camera devices:
ls -l /dev/ttyACM*
ls -l /dev/video*
The documented defaults are:
- Arduino serial: /dev/ttyACM0
- Baud rate: 115200
- USB camera: /dev/video0
- Dashboard port: 5000
Run:
python sentinel_dashboard.py
Open the Raspberry Pi's local IP address on port 5000 from a browser on the same trusted network.
The documented dashboard currently has no login/authentication. Do not expose it to the public internet or an untrusted network. Restrict access to a trusted private network until authentication and authorization are implemented.

Serial protocol documented in the project
Command	Expected / observed response
STATUS	A status line such as STATUS,SECURITY=NORMAL,ROBOT=STANDBY,MOVEMENT=STOP,STATE=SECURE.
SECURITY,LOCKDOWN	Observed: OK,SECURITY=LOCKDOWN.
SECURITY,NORMAL	Observed: OK,SECURITY=NORMAL,ROBOT=STANDBY.
MOVE,FORWARD while locked down	Observed in one tested serial path: ERR,LOCKDOWN; movement remained stopped.


Opening a serial port may reset the Arduino. Allow for startup time and refresh status after connecting.
Demonstrated results and evidence boundaries
Feature	Status in the project report
USB webcam detected and OpenCV capture	Implemented / observed; /dev/video0, 640×480.
Browser dashboard	Implemented / observed.
Live camera display	Implemented / observed.
Pi-to-Arduino serial status	Implemented / observed.
SECURITY,LOCKDOWN and SECURITY,NORMAL	Implemented / observed.
MOVE,FORWARD rejected during serial LOCKDOWN	Passed for the one tested serial path.
Manual device inventory and CSV export	Implemented / observed; inventory is not authentication.
HTTP movement request blocked during LOCKDOWN	Not verified.
Arduino Wi-Fi control path blocked during LOCKDOWN	Not verified.
Autonomous movement path blocked during LOCKDOWN	Not verified.
Login, authentication, role-based access	Not implemented in the documented dashboard.
Automatic attacker detection / comprehensive network discovery	Not implemented.
Quantitative security accuracy, false-positive rate, response latency	Not yet measured.


Do not describe this prototype as a validated intrusion-detection system or claim that all attackers are detected. The current evidence supports monitoring, logging, device inventory, and one tested controller-side lockdown command path.
Safety notes
- Disconnect the motor battery during firmware, serial, and security-interface tests.
- Use a physical emergency stop before any motor-powered test.
- Verify common ground, motor-driver wiring, supply voltage, current limits, and regulator-jumper configuration.
- Do not connect competing regulated 5 V supplies together without checking the circuit.
- Keep the Pi on its intended 5 V supply; do not assume the motor driver's 5 V output can safely power the Pi.
- Test movement commands with wheels raised or the motor supply disconnected before floor testing.
- Software lockdown does not replace physical power isolation or an emergency stop.
Future work
1. Verify lockdown across serial, dashboard HTTP, Arduino Wi-Fi, and autonomous movement paths.
2. Add login, secure sessions, CSRF protection, and server-side authorization.
3. Bind owner identity to credentials or cryptographic keys rather than IP/MAC labels.
4. Add transparent rule-based alerts for defined events such as repeated failed logins, serial disconnects, camera loss, or unusual command rates.
5. Add incident summaries and repeatable test exports.
6. Measure lockdown response time, unauthorized-command rejection rate, false positives, CPU/RAM overhead, and recovery after disconnect/reset.
7. Document each test with timestamps, test conditions, and observed output.
Source-code provenance
The two source files in this repository were extracted from Appendix C of the supplied SENTINEL: Security Monitoring and Lockdown Framework for a Mobile Robot project report. The report redacts the Wi-Fi password and owner-control token; those placeholders are intentionally retained and must be configured locally.


Project contributors
- P. Harsha Vardhan Reddy
-AI & DS(CSP&S)
-Amrita Vishwa Vidyapeetham, Coimbatore
- 
