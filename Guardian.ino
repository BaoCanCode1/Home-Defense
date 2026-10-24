#include <IRremote.h>
#include <LiquidCrystal.h>
#include <Servo.h>

constexpr byte TRIG_PIN = 2;
constexpr byte ECHO_PIN = 3;
constexpr byte GREEN_LED_PIN = 4;
constexpr byte RED_LED_PIN = 5;
constexpr byte BUZZER_PIN = 6;
constexpr byte LCD_RS_PIN = 7;
constexpr byte LCD_ENABLE_PIN = 8;
constexpr byte LCD_D4_PIN = 9;
constexpr byte LCD_D5_PIN = 10;
constexpr byte LCD_D6_PIN = 11;
constexpr byte LCD_D7_PIN = 12;
constexpr byte IR_RECEIVE_PIN = A0;
constexpr byte SERVO_PIN = A1;

constexpr unsigned long REMOTE_POWER_CODE = 0xFFA25DUL;
constexpr unsigned long REMOTE_ZERO_CODE = 0xFF6897UL;
constexpr unsigned long REMOTE_ONE_CODE = 0xFF30CFUL;
constexpr float MAX_ALERT_DISTANCE_CM = 400.0;
constexpr float ENTRY_MARGIN_CM = 8.0;
constexpr unsigned long ECHO_TIMEOUT_US = 25000;
constexpr unsigned long SCAN_SETTLE_MS = 75;
constexpr unsigned long BUZZER_ON_MS = 200;
constexpr unsigned long BUZZER_OFF_MS = 300;
constexpr byte SCAN_MIN_ANGLE = 20;
constexpr byte SCAN_MAX_ANGLE = 160;
constexpr byte SCAN_STEP_ANGLE = 10;
constexpr byte SCAN_POINT_COUNT =
    ((SCAN_MAX_ANGLE - SCAN_MIN_ANGLE) / SCAN_STEP_ANGLE) + 1;
constexpr byte CALIBRATION_READINGS = 3;
constexpr byte REQUIRED_CLEAR_SWEEPS = 2;

LiquidCrystal lcd(LCD_RS_PIN, LCD_ENABLE_PIN, LCD_D4_PIN, LCD_D5_PIN,
                  LCD_D6_PIN, LCD_D7_PIN);
IRrecv irReceiver(IR_RECEIVE_PIN);
decode_results irResults;
Servo radarServo;

bool systemPoweredOn = true;
bool zoneOccupied = false;
bool presenceLatched = false;
bool sweepHasIntrusion = false;
bool soundEnabled = true;
bool baselineHasEcho[SCAN_POINT_COUNT] = {};
float baselineDistanceCm[SCAN_POINT_COUNT] = {};
byte clearSweepCount = 0;
byte scanIndex = 0;
int8_t scanDirection = 1;
bool buzzerOn = false;
bool statusDisplayInitialized = false;
bool displayedSystemPoweredOn = true;
bool displayedZoneOccupied = false;
unsigned long lastScanReadMs = 0;
unsigned long buzzerChangeAtMs = 0;

byte scanAngle(byte index) {
  return SCAN_MIN_ANGLE + index * SCAN_STEP_ANGLE;
}

float measureDistanceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  const unsigned long echoDurationUs = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (echoDurationUs == 0) {
    return -1.0;
  }
  return echoDurationUs * 0.0343 / 2.0;
}

void showDistance(float distanceCm) {
  lcd.setCursor(0, 1);
  if (distanceCm < 0.0) {
    lcd.print("No echo         ");
    return;
  }
  lcd.print("Dist: ");
  lcd.print(static_cast<int>(distanceCm + 0.5));
  lcd.print(" cm    ");
}

void showStatus() {
  if (statusDisplayInitialized && displayedSystemPoweredOn == systemPoweredOn &&
      displayedZoneOccupied == zoneOccupied) {
    return;
  }
  statusDisplayInitialized = true;
  displayedSystemPoweredOn = systemPoweredOn;
  displayedZoneOccupied = zoneOccupied;
  lcd.setCursor(0, 0);
  lcd.print("                ");
  lcd.setCursor(0, 0);
  if (!systemPoweredOn) {
    lcd.print("SYSTEM OFF");
  } else if (zoneOccupied) {
    lcd.print("ZONE OCCUPIED");
  } else {
    lcd.print("RADAR CLEAR");
  }
}

void calibrateRadar() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Clear area now");
  Serial.println("Keep radar area empty: calibrating...");

  for (byte index = 0; index < SCAN_POINT_COUNT; ++index) {
    radarServo.write(scanAngle(index));
    delay(180);
    float totalDistanceCm = 0.0;
    byte validReadings = 0;
    for (byte reading = 0; reading < CALIBRATION_READINGS; ++reading) {
      const float distanceCm = measureDistanceCm();
      if (distanceCm >= 0.0) {
        totalDistanceCm += distanceCm;
        ++validReadings;
      }
      delay(35);
    }
    baselineHasEcho[index] = validReadings > 0;
    baselineDistanceCm[index] = baselineHasEcho[index]
        ? totalDistanceCm / validReadings : -1.0;

    lcd.setCursor(0, 1);
    lcd.print("Scan ");
    lcd.print(index + 1);
    lcd.print("/");
    lcd.print(SCAN_POINT_COUNT);
    lcd.print("       ");
  }

  scanIndex = 0;
  scanDirection = 1;
  radarServo.write(scanAngle(scanIndex));
  lcd.clear();
}

bool readingShowsIntrusion(byte index, float distanceCm) {
  if (distanceCm < 0.0 || distanceCm > MAX_ALERT_DISTANCE_CM) {
    return false;
  }
  if (!baselineHasEcho[index]) {
    return true;
  }
  return distanceCm <= baselineDistanceCm[index] - ENTRY_MARGIN_CM;
}

void updateZone();

void updatePresence(byte index, float distanceCm) {
  if (readingShowsIntrusion(index, distanceCm)) {
    presenceLatched = true;
    sweepHasIntrusion = true;
    clearSweepCount = 0;
  }
}

void finishSweep() {
  if (sweepHasIntrusion) {
    clearSweepCount = 0;
  } else if (presenceLatched && ++clearSweepCount >= REQUIRED_CLEAR_SWEEPS) {
    presenceLatched = false;
    clearSweepCount = 0;
  }
  sweepHasIntrusion = false;
}

void resetGuardian() {
  systemPoweredOn = true;
  soundEnabled = true;
  zoneOccupied = false;
  presenceLatched = false;
  sweepHasIntrusion = false;
  clearSweepCount = 0;
  buzzerOn = false;
  digitalWrite(BUZZER_PIN, LOW);
  statusDisplayInitialized = false;
  Serial.println("SYSTEM RESET: calibrating radar");
  calibrateRadar();
  updateZone();
}

void updateZone() {
  const bool previousZoneState = zoneOccupied;
  zoneOccupied = systemPoweredOn && presenceLatched;

  if (!systemPoweredOn) {
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, LOW);
    buzzerOn = false;
    digitalWrite(BUZZER_PIN, LOW);
  } else if (zoneOccupied) {
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, HIGH);
    if (!previousZoneState && soundEnabled) {
      Serial.println("ALERT: Zone occupied");
      buzzerOn = true;
      digitalWrite(BUZZER_PIN, HIGH);
      buzzerChangeAtMs = millis() + BUZZER_ON_MS;
    }
  } else {
    digitalWrite(GREEN_LED_PIN, HIGH);
    digitalWrite(RED_LED_PIN, LOW);
    buzzerOn = false;
    digitalWrite(BUZZER_PIN, LOW);
    if (previousZoneState) {
      Serial.println("CLEAR: Zone is empty");
    }
  }
  showStatus();
}

void updateBuzzer(unsigned long nowMs) {
  if (!zoneOccupied || !soundEnabled ||
      static_cast<long>(nowMs - buzzerChangeAtMs) < 0) {
    if (!soundEnabled) {
      buzzerOn = false;
      digitalWrite(BUZZER_PIN, LOW);
    }
    return;
  }
  buzzerOn = !buzzerOn;
  digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);
  buzzerChangeAtMs = nowMs + (buzzerOn ? BUZZER_ON_MS : BUZZER_OFF_MS);
}

void printRadarReading(byte angle, float distanceCm, bool targetDetected) {
  Serial.print("Radar: ");
  Serial.print(angle);
  Serial.print(" deg | Distance: ");
  if (distanceCm < 0.0) {
    Serial.print("no echo");
  } else {
    Serial.print(distanceCm, 1);
    Serial.print(" cm");
  }
  Serial.print(" | Target: ");
  Serial.print(targetDetected ? "YES" : "NO");
  Serial.print(" | Zone: ");
  Serial.println(zoneOccupied ? "OCCUPIED" : (systemPoweredOn ? "CLEAR" : "OFF"));
}

void advanceScan() {
  if (scanIndex == SCAN_POINT_COUNT - 1 && scanDirection > 0) {
    scanDirection = -1;
  } else if (scanIndex == 0 && scanDirection < 0) {
    scanDirection = 1;
  }
  scanIndex += scanDirection;
  radarServo.write(scanAngle(scanIndex));
}

void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  Serial.begin(9600);
  lcd.begin(16, 2);
  irReceiver.enableIRIn();
  radarServo.attach(SERVO_PIN);
  delay(300);
  calibrateRadar();
  updateZone();

  Serial.println("Guardian radar monitor");
  Serial.println("Servo signal: A1; keep area empty during calibration");
  Serial.println("Remote POWER: software off/on | 0: reset | 1: mute buzzer");
}

void loop() {
  const unsigned long nowMs = millis();

  if (irReceiver.decode(&irResults)) {
    Serial.print("IR code: 0x");
    Serial.println(irResults.value, HEX);
    if (irResults.value == REMOTE_POWER_CODE) {
      systemPoweredOn = !systemPoweredOn;
      if (!systemPoweredOn) {
        presenceLatched = false;
        sweepHasIntrusion = false;
        clearSweepCount = 0;
        soundEnabled = false;
      } else {
        soundEnabled = true;
      }
      Serial.println(systemPoweredOn ? "SYSTEM ON" : "SYSTEM OFF");
      updateZone();
    } else if (irResults.value == REMOTE_ZERO_CODE) {
      resetGuardian();
    } else if (irResults.value == REMOTE_ONE_CODE) {
      soundEnabled = false;
      buzzerOn = false;
      digitalWrite(BUZZER_PIN, LOW);
      Serial.println("SOUND MUTED: monitoring continues");
    }
    irReceiver.resume();
  }

  if (systemPoweredOn && nowMs - lastScanReadMs >= SCAN_SETTLE_MS) {
    lastScanReadMs = nowMs;
    const byte angle = scanAngle(scanIndex);
    const float distanceCm = measureDistanceCm();
    const bool targetDetected = readingShowsIntrusion(scanIndex, distanceCm);
    updatePresence(scanIndex, distanceCm);

    const bool endOfSweep =
        (scanIndex == SCAN_POINT_COUNT - 1 && scanDirection > 0) ||
        (scanIndex == 0 && scanDirection < 0);
    if (endOfSweep) {
      finishSweep();
    }

    updateZone();
    showDistance(distanceCm);
    printRadarReading(angle, distanceCm, targetDetected);
    advanceScan();
  }

  updateBuzzer(nowMs);
}
