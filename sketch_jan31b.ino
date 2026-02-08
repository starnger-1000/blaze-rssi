#include <WiFi.h>

/* ================= CONFIG ================= */

const char* HOTSPOT_SSID = "ESP32_RSSI";
const char* HOTSPOT_PASSWORD = "";   // Open hotspot

#define WIFI_CHANNEL 6   // Fix channel to avoid hopping

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n===== UAV RSSI TRANSMITTER =====");

  // WiFi setup
  WiFi.mode(WIFI_AP);

  // 🔥 Set MAX TX Power (IMPORTANT)
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  // Start AP with fixed channel
  WiFi.softAP(HOTSPOT_SSID, HOTSPOT_PASSWORD, WIFI_CHANNEL);

  IPAddress IP = WiFi.softAPIP();

  Serial.println("✅ Hotspot started");
  Serial.print("SSID      : "); Serial.println(HOTSPOT_SSID);
  Serial.print("Channel   : "); Serial.println(WIFI_CHANNEL);
  Serial.print("TX Power  : 19.5 dBm (MAX)");
  Serial.println();
  Serial.print("IP        : ");
  Serial.println(IP);
  Serial.println("--------------------------------");
  Serial.println("📡 Broadcasting WiFi beacon...");
}

void loop() {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 10000) {
    lastPrint = millis();
    Serial.print("📡 Broadcasting | Clients: ");
    Serial.println(WiFi.softAPgetStationNum());
  }

  delay(1000);  // harmless, AP keeps broadcasting
}