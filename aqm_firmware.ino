// ESP32 Air Quality Monitor - MPU6050 (I2C) temperature + simulated humidity
// Set ENABLE_BLE to 0 for Wokwi (no BLE support), 1 for a real ESP32 board.

#include <Wire.h>
#include <math.h>

#define ENABLE_BLE 0                 // <-- 0 = Wokwi, 1 = real board
#define DEVICE_NAME "AQM-Balakumar"   // <-- replace with your first name

#define SDA_PIN 21
#define SCL_PIN 22
#define MPU_ADDR 0x68
#define REG_PWR_MGMT_1 0x6B
#define REG_TEMP_OUT_H 0x41

#define WINDOW 5
#define MIN_INTERVAL_S 1
#define MAX_INTERVAL_S 3600

// Custom 128-bit UUIDs (randomly generated; change if you like)
#define SERVICE_UUID  "6e1a0001-b5a3-f393-e0a9-e50e24dcca9e"
#define TEMP_UUID     "6e1a0002-b5a3-f393-e0a9-e50e24dcca9e"
#define HUM_UUID      "6e1a0003-b5a3-f393-e0a9-e50e24dcca9e"
#define INTERVAL_UUID "6e1a0004-b5a3-f393-e0a9-e50e24dcca9e"

// ---------- State ----------
uint32_t intervalSec = 2;
unsigned long lastSample = 0;

float tempBuf[WINDOW], humBuf[WINDOW];
int bufCount = 0, bufIdx = 0;
float filtTemp = NAN, filtHum = NAN;

// ---------- Sensor ----------
bool mpuInit() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_PWR_MGMT_1);
  Wire.write(0x00);                  // wake from sleep
  return Wire.endTransmission() == 0;
}

// Reads temperature register pair directly. Returns NAN on I2C failure.
float readTemperatureC() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_TEMP_OUT_H);
  if (Wire.endTransmission(false) != 0) return NAN;
  if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)2) != 2) return NAN;
  int16_t raw = (Wire.read() << 8) | Wire.read();
  return raw / 340.0f + 36.53f;      // datasheet formula
}

float readSimulatedHumidity() {
  float base = 50.0f + 10.0f * sinf(millis() / 30000.0f);
  float noise = (random(-100, 101)) / 100.0f;
  return constrain(base + noise, 0.0f, 100.0f);
}

// ---------- Filtering ----------
float average(const float *b, int n) {
  float s = 0;
  for (int i = 0; i < n; i++) s += b[i];
  return s / n;
}

void pushSample(float t, float h) {
  tempBuf[bufIdx] = t;
  humBuf[bufIdx] = h;
  bufIdx = (bufIdx + 1) % WINDOW;
  if (bufCount < WINDOW) bufCount++;
  filtTemp = average(tempBuf, bufCount);
  filtHum = average(humBuf, bufCount);
}

// ---------- BLE ----------
#if ENABLE_BLE
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

BLECharacteristic *tempChar, *humChar, *intervalChar;
bool deviceConnected = false;

void setIntervalSeconds(uint32_t s);  // forward declaration

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer *) override { deviceConnected = true; }
  void onDisconnect(BLEServer *s) override {
    deviceConnected = false;
    s->getAdvertising()->start();     // keep advertising
  }
};

// Interval: write 1 byte (uint8) or 2 bytes (uint16, little-endian), seconds.
class IntervalCB : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    auto v = c->getValue();
    uint32_t s = 0;
    if (v.length() == 1) s = (uint8_t)v[0];
    else if (v.length() >= 2) s = (uint8_t)v[0] | ((uint8_t)v[1] << 8);
    else return;
    setIntervalSeconds(s);
  }
};

void bleInit() {
  BLEDevice::init(DEVICE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCB());
  BLEService *svc = server->createService(SERVICE_UUID);

  tempChar = svc->createCharacteristic(
      TEMP_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  tempChar->addDescriptor(new BLE2902());

  humChar = svc->createCharacteristic(
      HUM_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  humChar->addDescriptor(new BLE2902());

  intervalChar = svc->createCharacteristic(
      INTERVAL_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  intervalChar->setCallbacks(new IntervalCB());
  uint16_t iv = intervalSec;
  intervalChar->setValue((uint8_t *)&iv, 2);

  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();
  Serial.println("BLE advertising as " DEVICE_NAME);
}

// Temp/humidity are sent as ASCII text (e.g. "24.53") so nRF Connect shows them readably.
void blePublish() {
  char tb[16], hb[16];
  snprintf(tb, sizeof(tb), "%.2f", filtTemp);
  snprintf(hb, sizeof(hb), "%.2f", filtHum);
  tempChar->setValue((uint8_t *)tb, strlen(tb));
  humChar->setValue((uint8_t *)hb, strlen(hb));
  if (deviceConnected) {
    tempChar->notify();
    humChar->notify();
  }
}

void bleUpdateInterval() {
  uint16_t iv = intervalSec;
  intervalChar->setValue((uint8_t *)&iv, 2);
}
#else
void bleInit() {}
void blePublish() {}
void bleUpdateInterval() {}
#endif

// ---------- Interval control (shared by BLE + Serial) ----------
void setIntervalSeconds(uint32_t s) {
  if (s < MIN_INTERVAL_S || s > MAX_INTERVAL_S) {
    Serial.printf("Rejected interval %u (allowed %d-%d s)\n", (unsigned)s,
                  MIN_INTERVAL_S, MAX_INTERVAL_S);
    return;
  }
  intervalSec = s;
  bleUpdateInterval();
  Serial.printf("Sampling interval set to %u s\n", (unsigned)intervalSec);
}

// ---------- Serial commands ----------
void handleCommand(String line) {
  line.trim();
  line.toUpperCase();
  if (line == "GET TEMP") {
    Serial.printf("Temperature: %.2f C\n", filtTemp);
  } else if (line == "GET HUM") {
    Serial.printf("Humidity: %.2f %%\n", filtHum);
  } else if (line.startsWith("SET INTERVAL ")) {
    long s = line.substring(13).toInt();
    setIntervalSeconds(s > 0 ? (uint32_t)s : 0);
  } else if (line.length()) {
    Serial.println("Commands: GET TEMP | GET HUM | SET INTERVAL <seconds>");
  }
}

void pollSerial() {
  static String buf;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      handleCommand(buf);
      buf = "";
    } else {
      buf += c;
    }
  }
}

// ---------- Main ----------
void setup() {
  Serial.begin(115200);
  delay(200);
  randomSeed(esp_random());
  if (!mpuInit()) Serial.println("WARNING: MPU6050 not found (check wiring)");
  bleInit();
  Serial.println("Ready. Commands: GET TEMP | GET HUM | SET INTERVAL <seconds>");
}

void loop() {
  pollSerial();

  if (millis() - lastSample >= intervalSec * 1000UL) {
    lastSample = millis();
    float t = readTemperatureC();
    float h = readSimulatedHumidity();
    if (isnan(t)) {
      Serial.println("I2C read failed");
    } else {
      pushSample(t, h);
      Serial.printf("T=%.2f C  H=%.2f %%  (avg of %d)\n", filtTemp, filtHum, bufCount);
      blePublish();
    }
  }
}
