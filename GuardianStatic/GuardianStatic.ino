#include <IRremote.h>
#include <LiquidCrystal.h>

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
constexpr byte FIXED_SCAN_ANGLE = 90;

constexpr unsigned long SERIAL_BAUD_RATE = 115200;
constexpr unsigned long REMOTE_POWER_CODE = 0xFFA25DUL;
constexpr unsigned long REMOTE_ZERO_CODE = 0xFF6897UL;
constexpr unsigned long REMOTE_ONE_CODE = 0xFF30CFUL;
constexpr float SENSOR_MAX_DISTANCE_CM = 400.0;
constexpr float PROTECTED_ZONE_DISTANCE_CM = 400.0;
constexpr float ENTRY_MARGIN_CM = 8.0;
constexpr float MIN_VALID_DISTANCE_CM = 2.0;
constexpr float MAX_BASELINE_SAMPLE_SPREAD_CM = 25.0;
constexpr unsigned long ECHO_TIMEOUT_US = 26000;
constexpr unsigned long BETWEEN_SENSOR_READINGS_MS = 60;
constexpr unsigned long BUZZER_ON_MS = 200;
constexpr unsigned long BUZZER_OFF_MS = 300;
constexpr byte FILTER_SAMPLE_COUNT = 7;
constexpr byte MIN_VALID_SAMPLE_COUNT = 5;
constexpr byte BASELINE_BATCH_COUNT = 3;
constexpr byte REQUIRED_ENTRY_CONFIRMATIONS = 2;
constexpr byte REQUIRED_CLEAR_CONFIRMATIONS = 3;

LiquidCrystal lcd(LCD_RS_PIN, LCD_ENABLE_PIN, LCD_D4_PIN, LCD_D5_PIN,
                  LCD_D6_PIN, LCD_D7_PIN);
IRrecv irReceiver(IR_RECEIVE_PIN);
decode_results irResults;

bool systemPoweredOn = true;
bool soundEnabled = true;
bool calibrationActive = false;
bool baselineReady = false;
bool presenceLatched = false;
bool zoneOccupied = false;
bool buzzerOn = false;
bool statusDisplayInitialized = false;
bool displayedSystemPoweredOn = true;
bool displayedZoneOccupied = false;
float baselineSamplesCm[BASELINE_BATCH_COUNT] = {};
float baselineDistanceCm = -1.0;
byte calibrationSampleIndex = 0;
byte entryConfirmationCount = 0;
byte clearConfirmationCount = 0;
unsigned long buzzerChangeAtMs = 0;

float measureDistanceOnceCm() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  const unsigned long echoDurationUs = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT_US);
  if (echoDurationUs == 0) {
    return -1.0;
  }

  const float distanceCm = echoDurationUs * 0.0343 / 2.0;
  if (distanceCm < MIN_VALID_DISTANCE_CM ||
      distanceCm > SENSOR_MAX_DISTANCE_CM) {
    return -1.0;
  }
  return distanceCm;
}

float measureMedianDistanceCm() {
  float validDistances[FILTER_SAMPLE_COUNT] = {};
  byte validReadingCount = 0;

  for (byte readingIndex = 0; readingIndex < FILTER_SAMPLE_COUNT;
       ++readingIndex) {
    const float distanceCm = measureDistanceOnceCm();
    if (distanceCm >= 0.0) {
      validDistances[validReadingCount] = distanceCm;
      ++validReadingCount;
    }
    if (readingIndex + 1 < FILTER_SAMPLE_COUNT) {
      delay(BETWEEN_SENSOR_READINGS_MS);
    }
  }

  if (validReadingCount < MIN_VALID_SAMPLE_COUNT) {
    return -1.0;
  }

  for (byte currentIndex = 1; currentIndex < validReadingCount;
       ++currentIndex) {
    const float selectedDistance = validDistances[currentIndex];
    byte insertIndex = currentIndex;
    while (insertIndex > 0 &&
           validDistances[insertIndex - 1] > selectedDistance) {
      validDistances[insertIndex] = validDistances[insertIndex - 1];
      --insertIndex;
    }
    validDistances[insertIndex] = selectedDistance;
  }

  return validDistances[validReadingCount / 2];
}

void showStatus() {
  if (statusDisplayInitialized &&
      displayedSystemPoweredOn == systemPoweredOn &&
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
  } else if (calibrationActive) {
    lcd.print("CALIBRATING");
  } else if (!baselineReady) {
    lcd.print("CHECK CALIBRATION");
  } else if (zoneOccupied) {
    lcd.print("ZONE OCCUPIED");
  } else {
    lcd.print("STATIC CLEAR");
  }
}

void showDistance(float distanceCm) {
  lcd.setCursor(0, 1);
  if (distanceCm < 0.0) {
    lcd.print("Distance: no echo");
    return;
  }
  lcd.print("Distance: ");
  lcd.print(static_cast<int>(distanceCm + 0.5));
  lcd.print(" cm   ");
}

void updateZone() {
  const bool previousZoneState = zoneOccupied;
  zoneOccupied = systemPoweredOn && baselineReady && presenceLatched;

  if (!systemPoweredOn || calibrationActive || !baselineReady) {
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

void startCalibration() {
  calibrationActive = true;
  baselineReady = false;
  baselineDistanceCm = -1.0;
  calibrationSampleIndex = 0;
  entryConfirmationCount = 0;
  clearConfirmationCount = 0;
  presenceLatched = false;
  zoneOccupied = false;
  buzzerOn = false;
  digitalWrite(BUZZER_PIN, LOW);
  for (byte index = 0; index < BASELINE_BATCH_COUNT; ++index) {
    baselineSamplesCm[index] = -1.0;
  }

  lcd.clear();
  statusDisplayInitialized = false;
  showStatus();
  Serial.println("Keep the fixed sensor area empty for calibration");
}

void finishCalibration() {
  float minimumDistanceCm = SENSOR_MAX_DISTANCE_CM;
  float maximumDistanceCm = 0.0;
  float distanceSumCm = 0.0;
  byte validBatchCount = 0;

  for (byte index = 0; index < BASELINE_BATCH_COUNT; ++index) {
    const float sampleDistanceCm = baselineSamplesCm[index];
    if (sampleDistanceCm < 0.0) {
      continue;
    }
    if (sampleDistanceCm < minimumDistanceCm) {
      minimumDistanceCm = sampleDistanceCm;
    }
    if (sampleDistanceCm > maximumDistanceCm) {
      maximumDistanceCm = sampleDistanceCm;
    }
    distanceSumCm += sampleDistanceCm;
    ++validBatchCount;
  }

  calibrationActive = false;
  baselineReady = validBatchCount >= 2 &&
                  maximumDistanceCm - minimumDistanceCm <=
                      MAX_BASELINE_SAMPLE_SPREAD_CM;
  if (baselineReady) {
    baselineDistanceCm = distanceSumCm / validBatchCount;
    Serial.print("Calibration complete | Baseline: ");
    Serial.print(baselineDistanceCm, 1);
    Serial.println(" cm");
  } else {
    Serial.println("Calibration unstable; clear the area and press remote 0");
  }

  statusDisplayInitialized = false;
  updateZone();
}

void updateCalibration() {
  const float sampleDistanceCm = measureMedianDistanceCm();
  baselineSamplesCm[calibrationSampleIndex] = sampleDistanceCm;
  Serial.print("Calibration sample ");
  Serial.print(calibrationSampleIndex + 1);
  Serial.print("/");
  Serial.print(BASELINE_BATCH_COUNT);
  Serial.print(" | Distance: ");
  if (sampleDistanceCm >= 0.0) {
    Serial.print(sampleDistanceCm, 1);
    Serial.println(" cm");
  } else {
    Serial.println("no reliable echo");
  }

  ++calibrationSampleIndex;
  if (calibrationSampleIndex >= BASELINE_BATCH_COUNT) {
    finishCalibration();
  }
}

bool readingShowsIntrusion(float distanceCm) {
  if (!baselineReady || distanceCm < 0.0 ||
      distanceCm > PROTECTED_ZONE_DISTANCE_CM) {
    return false;
  }
  return distanceCm <= baselineDistanceCm - ENTRY_MARGIN_CM;
}

void printReading(float distanceCm, bool targetConfirmed) {
  Serial.print("Radar: ");
  Serial.print(FIXED_SCAN_ANGLE);
  Serial.print(" deg | Distance: ");
  if (distanceCm < 0.0) {
    Serial.print("no echo");
  } else {
    Serial.print(distanceCm, 1);
    Serial.print(" cm");
  }
  Serial.print(" | Target: ");
  Serial.print(targetConfirmed ? "YES" : "NO");
  Serial.print(" | Zone: ");
  Serial.println(zoneOccupied ? "OCCUPIED" : "CLEAR");
}

void updateBuzzer(unsigned long nowMs) {
  if (!zoneOccupied || !soundEnabled) {
    buzzerOn = false;
    digitalWrite(BUZZER_PIN, LOW);
    return;
  }
  if (static_cast<long>(nowMs - buzzerChangeAtMs) < 0) {
    return;
  }

  buzzerOn = !buzzerOn;
  digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);
  buzzerChangeAtMs = nowMs + (buzzerOn ? BUZZER_ON_MS : BUZZER_OFF_MS);
}

void handleRemote() {
  if (!irReceiver.decode(&irResults)) {
    return;
  }

  Serial.print("IR code: 0x");
  Serial.println(irResults.value, HEX);
  if (irResults.value == REMOTE_POWER_CODE) {
    systemPoweredOn = !systemPoweredOn;
    if (systemPoweredOn) {
      soundEnabled = true;
      startCalibration();
      Serial.println("SYSTEM ON");
    } else {
      calibrationActive = false;
      baselineReady = false;
      presenceLatched = false;
      soundEnabled = false;
      Serial.println("SYSTEM OFF");
      updateZone();
    }
  } else if (irResults.value == REMOTE_ZERO_CODE) {
    systemPoweredOn = true;
    soundEnabled = true;
    startCalibration();
    Serial.println("SYSTEM RESET: calibrating");
  } else if (irResults.value == REMOTE_ONE_CODE) {
    soundEnabled = false;
    buzzerOn = false;
    digitalWrite(BUZZER_PIN, LOW);
    Serial.println("SOUND MUTED: monitoring continues");
  }
  irReceiver.resume();
}

void setup() {
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(TRIG_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  Serial.begin(SERIAL_BAUD_RATE);
  lcd.begin(16, 2);
  irReceiver.enableIRIn();

  Serial.println("Guardian static protected-zone monitor");
  Serial.println("Fixed sensor; no servo rotation");
  Serial.println("Serial speed: 115200 baud");
  Serial.println("Remote POWER: software off/on | 0: recalibrate | 1: mute");
  startCalibration();
}

void loop() {
  handleRemote();

  if (systemPoweredOn && calibrationActive) {
    updateCalibration();
  } else if (systemPoweredOn && baselineReady) {
    const float distanceCm = measureMedianDistanceCm();
    const bool targetCandidate = readingShowsIntrusion(distanceCm);
    if (targetCandidate) {
      clearConfirmationCount = 0;
      if (entryConfirmationCount < REQUIRED_ENTRY_CONFIRMATIONS) {
        ++entryConfirmationCount;
      }
      if (entryConfirmationCount >= REQUIRED_ENTRY_CONFIRMATIONS) {
        presenceLatched = true;
      }
    } else {
      entryConfirmationCount = 0;
      if (presenceLatched) {
        if (clearConfirmationCount < REQUIRED_CLEAR_CONFIRMATIONS) {
          ++clearConfirmationCount;
        }
        if (clearConfirmationCount >= REQUIRED_CLEAR_CONFIRMATIONS) {
          presenceLatched = false;
          clearConfirmationCount = 0;
        }
      }
    }

    updateZone();
    printReading(distanceCm, targetCandidate && presenceLatched);
    showDistance(distanceCm);
  }

  updateBuzzer(millis());
}
