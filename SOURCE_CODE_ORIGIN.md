# Source-code origin and notes

The files `firmware/sentinel_arduino.ino` and `raspberry_pi/sentinel_dashboard.py` are extracted from Appendix C (Source Code Listings) of the supplied SENTINEL project report.

- C.1: Arduino UNO R4 WiFi firmware (`sentinel_arduino.ino`)
- C.2: Raspberry Pi Flask dashboard (`sentinel_dashboard.py`)

The source listings were transferred from the report without intentionally redesigning the program logic. The report replaces the deployed Wi-Fi password and owner-control token with redacted placeholders. Set your own credentials locally before uploading or running the code. Never commit real passwords, tokens, or private network credentials.

The code should be reviewed and tested against the exact hardware wiring and installed library versions before use. In particular, verify motor direction, emergency-stop behaviour, serial reset/startup behaviour, and all movement-control paths with motor power disconnected.
