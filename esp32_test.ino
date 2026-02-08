#include <WiFi.h>
#include <ESP32Servo.h>
#include <PID_v1.h>  // Added PID library

/* ================= CONFIG ================= */

#define PAN_PIN   13
#define TILT_PIN  12

#define PAN_MIN   0
#define PAN_MAX   180

#define STEP_FINE  2
#define STEP_COARSE 5

#define RSSI_SAMPLES 3
#define DROP_THRESH  -8     // dBm
#define SETTLE_MS    40

// PID tuning parameters (adjust as needed for your setup)
#define PAN_KP 0.1
#define PAN_KI 0.01
#define PAN_KD 0.05
#define TILT_KP 0.1
#define TILT_KI 0.01
#define TILT_KD 0.05

/* ================= OBJECTS ================= */

Servo panServo;
Servo tiltServo;

// PID objects
double panInput = 0, panOutput = 0, panSetpoint = 0;
double tiltInput = 0, tiltOutput = 0, tiltSetpoint = 0;
PID panPID(&panInput, &panOutput, &panSetpoint, PAN_KP, PAN_KI, PAN_KD, DIRECT);
PID tiltPID(&tiltInput, &tiltOutput, &tiltSetpoint, TILT_KP, TILT_KI, TILT_KD, DIRECT);

/* ================= STATE ================= */

int panAngle = 90;
int tiltAngle = 90;  // Added tilt angle
int lockedRSSI = -1000;
int lockedSector = 1; // 0=LEFT, 1=CENTER, 2=RIGHT
bool locked = false;

/* ================= RSSI ================= */

int readRSSI() {
  int s = 0;
  for (int i = 0; i < RSSI_SAMPLES; i++) {
    s += WiFi.RSSI();
    delay(8);
  }
  return s / RSSI_SAMPLES;
}

/* ================= SECTOR ENERGY ================= */

int sectorEnergy(int startA, int endA) {
  int sum = 0;
  int cnt = 0;
  for (int a = startA; a <= endA; a += STEP_COARSE) {
    panServo.write(a);
    delay(SETTLE_MS);
    sum += readRSSI();
    cnt++;
  }
  return sum / cnt;
}

/* ================= FULL ACQUISITION ================= */

void acquireTarget() {
  Serial.println("\n🔍 ACQUIRING TARGET...");

  int eLeft   = sectorEnergy(0, 60);
  int eCenter = sectorEnergy(60, 120);
  int eRight  = sectorEnergy(120, 180);

  if (eLeft > eCenter && eLeft > eRight) lockedSector = 0;
  else if (eCenter > eRight)             lockedSector = 1;
  else                                   lockedSector = 2;

  int sMin = lockedSector * 60;
  int sMax = sMin + 60;

  int bestRSSI = -1000;
  for (int a = sMin; a <= sMax; a += STEP_COARSE) {
    panServo.write(a);
    delay(SETTLE_MS);
    int r = readRSSI();
    if (r > bestRSSI) {
      bestRSSI = r;
      panAngle = a;
    }
  }

  panServo.write(panAngle);

  // Added tilt scanning at the best pan angle
  int bestTiltRSSI = -1000;
  for (int t = 0; t <= 180; t += STEP_COARSE) {
    tiltServo.write(t);
    delay(SETTLE_MS);
    int r = readRSSI();
    if (r > bestTiltRSSI) {
      bestTiltRSSI = r;
      tiltAngle = t;
    }
  }
  tiltServo.write(tiltAngle);

  lockedRSSI = bestTiltRSSI;
  locked = true;

  Serial.printf("🎯 LOCKED | Sector %d | Pan %d° | Tilt %d° | RSSI %d\n",
                lockedSector, panAngle, tiltAngle, lockedRSSI);
}

/* ================= SETUP ================= */

void setup() {
  Serial.begin(115200);

  panServo.attach(PAN_PIN);
  tiltServo.attach(TILT_PIN);

  panServo.write(panAngle);
  tiltServo.write(tiltAngle);

  // Initialize PIDs
  panPID.SetMode(AUTOMATIC);
  panPID.SetOutputLimits(-10, 10);  // Limit output to prevent large jumps
  tiltPID.SetMode(AUTOMATIC);
  tiltPID.SetOutputLimits(-10, 10);

  WiFi.mode(WIFI_STA);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  WiFi.begin("ESP32_RSSI", "");

  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(200);
  }
  Serial.println("\n✅ Connected");

  acquireTarget();
}

/* ================= LOOP ================= */

void loop() {

  if (!locked) {
    acquireTarget();
    return;
  }

  /* ---- LOCAL TRACKING WITH PID ---- */
  // Pan tracking
  int leftA  = max(panAngle - STEP_FINE, lockedSector * 60);
  int rightA = min(panAngle + STEP_FINE, lockedSector * 60 + 60);

  panServo.write(leftA);
  delay(SETTLE_MS);
  int rL = readRSSI();

  panServo.write(rightA);
  delay(SETTLE_MS);
  int rR = readRSSI();

  panInput = rR - rL;  // Positive if right is stronger
  panPID.Compute();
  panAngle = constrain(panAngle + (int)panOutput, lockedSector * 60, lockedSector * 60 + 60);
  panServo.write(panAngle);

  // Tilt tracking (full range)
  int upA = max(tiltAngle - STEP_FINE, 0);
  int downA = min(tiltAngle + STEP_FINE, 180);

  tiltServo.write(upA);
  delay(SETTLE_MS);a
  int rU = readRSSI();

  tiltServo.write(downA);
  delay(SETTLE_MS);
  int rD = readRSSI();

  tiltInput = rD - rU;  // Positive if down is stronger
  tiltPID.Compute();
  tiltAngle = constrain(tiltAngle + (int)tiltOutput, 0, 180);
  tiltServo.write(tiltAngle);

  int rNow = readRSSI();

  if (rNow < lockedRSSI + DROP_THRESH) {
    Serial.println("⚠️ SIGNAL DROP — REACQUIRE");
    locked = false;
    return;
  }

  lockedRSSI = rNow;

  Serial.printf("PAN %3d° | TILT %3d° | RSSI %d\n", panAngle, tiltAngle, rNow);
}