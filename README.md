# Guardian — Protected-Zone Monitor

Guardian uses an Arduino UNO R3, a servo, and an ultrasonic sensor to sweep across a restricted area like a simple radar. At startup, it learns the empty distance at each scan angle; keep people and objects out of the radar area during this short calibration. When armed, a new object inside the zone marks it occupied and starts a repeating beep. The red LED and LCD keep showing occupied even if the object stands still. Press the Elegoo remote's **POWER** button to turn alerts on or off.

The sketch is written in Arduino C++, which is C++ compiled for the UNO. It uses the Arduino core's built-in `LiquidCrystal` library for a standard 16-pin, parallel LCD1602.

## Parts

- Arduino UNO R3 and USB cable
- HC-SR04-compatible ultrasonic sensor
- Small servo motor, such as an SG90, plus a secure mount for the sensor
- LCD1602, standard parallel interface
- Green LED and red LED, each with a 220–330 Ω series resistor
- Buzzer (active buzzer assumed)
- Elegoo IR remote and IR receiver module
- Breadboard and jumper wires

## Wiring

| Part | Pin / connection |
| --- | --- |
| Ultrasonic sensor VCC / GND | 5V / GND |
| Ultrasonic sensor TRIG / ECHO | D2 / D3 |
| Servo signal / power / ground | A1 / separate regulated 5V / common GND |
| Green LED anode through resistor / cathode | D4 / GND |
| Red LED anode through resistor / cathode | D5 / GND |
| Active buzzer + / − | D6 / GND |
| IR receiver signal (`Y` or `S`) / GND (`G`) / power (`R`) | A0 / GND / 3.3V |
| LCD1602 VSS / VDD / VO | GND / 5V / contrast potentiometer wiper |
| LCD1602 RS / E / D4 / D5 / D6 / D7 | D7 / D8 / D9 / D10 / D11 / D12 |
| LCD1602 RW | GND |
| LCD1602 LED A / K | 5V through a suitable resistor / GND (check module) |

Use a 10 kΩ potentiometer across 5V and GND for LCD contrast, with its wiper connected to VO. Connect every component's ground to UNO GND. Power the servo from a separate regulated 5V supply rated for at least 1 A, and connect that supply's GND to Arduino GND. Add a 470–1000 µF capacitor across that servo supply near the servo. The UNO 5V pin often cannot provide enough current for a servo plus the LCD, sensor, LEDs, and buzzer; that causes missed movement, resets, dim LCD text, or shaky scans.

## Upload and try it

1. Wire the circuit with USB disconnected.
2. Open `Guardian.ino` in the Arduino IDE, select **Arduino UNO**, choose its serial port, and upload.
3. Open **Tools → Serial Monitor** and set the baud rate to **115200**.
4. Keep the radar area empty during startup. The servo moves through 15 positions and saves the distance at each one; calibration takes about 15 seconds. The LCD shows the slot number and angle.
5. After calibration, the sensor repeatedly sweeps from 20° to 160°. Move your hand into its path. The buzzer repeats while the zone is occupied (200 ms on, 300 ms off), even if your hand stays still. Move your hand out; after one clear sweep pass it returns to `RADAR CLEAR` and green.
6. Point the remote at the IR receiver and briefly tap **POWER** to enter `SYSTEM OFF`: scanning, LEDs, and buzzer stop, but the IR receiver remains ready. Tap **POWER** again to recalibrate and resume monitoring. Press **0** to reset the Guardian software and repeat its empty-area calibration. Calibration now runs in small steps so the main loop can continue reading the remote. Press **1** to mute the buzzer while the sensor, LEDs, LCD, and phone dashboard continue working. Reset or turn the system off and back on to restore sound.

The sketch uses the legacy `IRremote.h` API and is written for IRremote 2.6.0. The expected raw codes are POWER `0xFFA25D`, 0 `0xFF6897`, and 1 `0xFF30CF`. The Serial Monitor prints every received code, so replace these constants in `Guardian.ino` if your remote reports different values. The IR receiver module in the Elegoo lesson labels its pins G (ground), Y (signal), and R (3.3V); follow your module's labels if its layout differs.

The sensor reads up to 4 m, but the protected zone is set to 2 m. During calibration the radar saves a separate background distance in each of its 15 scan positions. It waits for the servo to reach each angle, takes five readings with at least 60 ms between pings, discards the highest and lowest, and averages the rest to reduce false echoes. During normal scanning it takes three readings and uses their middle value. An object inside 2 m must appear at least 8 cm closer than the saved background at that position to count as entering. Positions that had no echo during calibration are ignored to prevent random echoes from causing false alarms. The saved distances stay in memory until the Arduino is reset or powered off, so recalibrate with the area empty after startup. The system clears after one complete sweep pass finds no intrusion; a new detection keeps it occupied. If the area is not empty during calibration, the saved background will be wrong. This is a simple front-facing scan, not a camera image; it detects objects, not people.

## Phone dashboard through the laptop

`guardian_phone_dashboard.py` turns the laptop connected to the UNO into a small local website. Your phone does not need an app, but the phone and laptop must be connected to the same Wi-Fi. The laptop must stay connected to the UNO by USB while the dashboard is in use.

1. Upload `Guardian.ino`, then close Arduino IDE's Serial Monitor. Only one program can use the UNO's serial port at a time.
2. Install Python 3 from [python.org](https://www.python.org/downloads/windows/) if it is not already installed. During installation, select **Add Python to PATH**. Then, in Windows Terminal or PowerShell, open this project folder and run `python -m pip install -r requirements.txt` once.
3. Find the UNO port in Arduino IDE under **Tools → Port**, such as `COM3`.
4. Run `python guardian_phone_dashboard.py COM3`, replacing `COM3` with your port. If you are not sure, run `python guardian_phone_dashboard.py --list-ports`.
5. The program prints a phone address like `http://192.168.1.25:8000`. Type that complete address into your phone's browser. If Windows asks about firewall access, allow it on **Private networks** only.

The dashboard shows a moving green scan line, the latest angle and distance, and the zone state. Blue dots are normal echoes such as a wall; red dots are readings that differ enough from the saved empty-area baseline to count as a protected-zone target. It is only available on your local Wi-Fi; it does not send information to the internet.

## LCD interface note

This starter assumes the LCD1602 has the usual 16-pin parallel interface. If your module has an I2C backpack with only four connections (often labeled GND, VCC, SDA, SCL), the wiring and LCD library need to be changed; UNO R3 I2C uses SDA=A4 and SCL=A5. Check the actual module before wiring.
