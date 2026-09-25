#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <nvs_flash.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASSWORD, FIREBASE_HOST, FIREBASE_AUTH
                        // -> salin dari secrets.example.h, isi kredensial sendiri,
                        //    JANGAN commit file secrets.h ke Git

// ─── Path Firebase ───────────────────────────────────────────
#define FIREBASE_GPS_PATH    "/tongkat/gps/latest"
#define FIREBASE_BUTTON_PATH "/tongkat/button/status"

// ─── Pin ─────────────────────────────────────────────────────
#define GPS_RX_PIN   16   // GPS modul TX → ESP32 RX2
#define GPS_TX_PIN   17   // GPS modul RX ← ESP32 TX2
#define GPS_BAUD     9600

#define BUZZER_PIN   26   // Output buzzer
#define BUTTON_PIN   27   // Input push button (INPUT_PULLUP)

// ─── Interval (ms) ───────────────────────────────────────────
#define INTERVAL_GPS           3000
#define INTERVAL_FIREBASE      5000
#define BUZZER_NOWIFI_ON       1000
#define BUZZER_NOWIFI_OFF      500
#define WIFI_RECONNECT_TIMEOUT 30000
#define DEBOUNCE_DELAY         50

// ─── Objek Hardware ──────────────────────────────────────────
TinyGPSPlus    gps;
HardwareSerial gpsSerial(2);   // UART2 = GPS

FirebaseData   fbdo;
FirebaseAuth   auth;
FirebaseConfig firebaseConfig;

// ─── State Sistem ────────────────────────────────────────────
bool          internetOK       = false;
bool          buttonState      = false;
bool          lastButtonRead   = HIGH;
unsigned long lastDebounceTime = 0;
bool          buttonPending    = false;
bool          buttonPendingVal = false;
bool          buzzerState      = false;
unsigned long lastBuzzerToggle = 0;
bool          wifiReconnecting   = false;
unsigned long wifiReconnectStart = 0;

struct LokasiGPS {
  double        latitude   = 0.0;
  double        longitude  = 0.0;
  double        altitude   = 0.0;
  double        speed      = 0.0;
  int           satellites = 0;
  bool          valid      = false;
  unsigned long timestamp  = 0;
};
LokasiGPS     lokasiSaatIni;
unsigned long lastGPSUpdate    = 0;
unsigned long lastFirebaseSend = 0;

// ============================================================
//  Buzzer
// ============================================================
void buzzerOn() {
  digitalWrite(BUZZER_PIN, HIGH);
  buzzerState = true;
}

void buzzerOff() {
  digitalWrite(BUZZER_PIN, LOW);
  buzzerState = false;
}

void kelolaBuzzer(unsigned long now) {
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long elapsed = now - lastBuzzerToggle;
    if (buzzerState)  { if (elapsed >= BUZZER_NOWIFI_ON)  { buzzerOff(); lastBuzzerToggle = now; } }
    else              { if (elapsed >= BUZZER_NOWIFI_OFF) { buzzerOn();  lastBuzzerToggle = now; } }
  } else {
    if (buzzerState) buzzerOff();
  }
}

// ============================================================
//  WiFi
// ============================================================
void inisialisasiFirebase();

void inisialisasiWiFi() {
  Serial.print("[WiFi] Menghubungkan ke "); Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    internetOK = true;
    Serial.println("\n[WiFi] Terhubung! IP: " + WiFi.localIP().toString());
  } else {
    internetOK = false;
    Serial.println("\n[WiFi] GAGAL. Sistem berjalan offline.");
  }
}

void kelolaReconnectWiFi(unsigned long now) {
  if (WiFi.status() == WL_CONNECTED) {
    if (wifiReconnecting) {
      Serial.println("[WiFi] Reconnect berhasil!");
      wifiReconnecting = false;
      internetOK = true;
      if (!Firebase.ready()) inisialisasiFirebase();
    }
    return;
  }
  internetOK = false;
  if (!wifiReconnecting) {
    WiFi.disconnect(true); delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    wifiReconnecting   = true;
    wifiReconnectStart = now;
    Serial.println("[WiFi] Memulai reconnect...");
  } else if (now - wifiReconnectStart >= WIFI_RECONNECT_TIMEOUT) {
    Serial.println("[WiFi] Timeout. Coba lagi nanti.");
    wifiReconnecting = false;
    WiFi.disconnect(true);
  }
}

// ============================================================
//  Firebase
// ============================================================
void inisialisasiFirebase() {
  firebaseConfig.host                       = FIREBASE_HOST;
  firebaseConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&firebaseConfig, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("[Firebase] Inisialisasi selesai.");
}

void kirimGPSKeFirebase() {
  if (WiFi.status() != WL_CONNECTED) { Serial.println("[Firebase] Skip GPS — WiFi putus."); return; }
  if (!lokasiSaatIni.valid)           { Serial.println("[Firebase] Skip GPS — belum fix.");  return; }
  FirebaseJson json;
  json.set("latitude",      lokasiSaatIni.latitude);
  json.set("longitude",     lokasiSaatIni.longitude);
  json.set("altitude",      lokasiSaatIni.altitude);
  json.set("speed_kmph",    lokasiSaatIni.speed);
  json.set("satellites",    lokasiSaatIni.satellites);
  json.set("timestamp/.sv", "timestamp");
  if (Firebase.RTDB.setJSON(&fbdo, FIREBASE_GPS_PATH, &json)) {
    internetOK = true;
    Serial.printf("[Firebase] GPS terkirim: %.6f, %.6f\n", lokasiSaatIni.latitude, lokasiSaatIni.longitude);
  } else {
    internetOK = false;
    Serial.printf("[Firebase] GAGAL GPS: %s\n", fbdo.errorReason().c_str());
  }
}

void kirimButtonKeFirebase(bool state) {
  if (WiFi.status() != WL_CONNECTED) {
    buttonPending = true; buttonPendingVal = state;
    Serial.println("[Firebase] Button pending — WiFi putus."); return;
  }
  if (Firebase.RTDB.setBool(&fbdo, FIREBASE_BUTTON_PATH, state)) {
    buttonPending = false;
    Serial.printf("[Firebase] Button: %s\n", state ? "true" : "false");
  } else {
    buttonPending = true; buttonPendingVal = state;
    Serial.printf("[Firebase] GAGAL Button: %s\n", fbdo.errorReason().c_str());
  }
}
// ============================================================
//  Button
// ============================================================
void kelolaButton(unsigned long now) {
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonRead) lastDebounceTime = now;
  lastButtonRead = reading;
  if ((now - lastDebounceTime) >= DEBOUNCE_DELAY) {
    bool pressedNow = (reading == LOW);
    if (pressedNow != buttonState) {
      buttonState = pressedNow;
      Serial.printf("[BUTTON] %s\n", buttonState ? "DITEKAN" : "LEPAS");
      kirimButtonKeFirebase(buttonState);
    }
  }
}
// ============================================================
//  GPS
// ============================================================
void bacaGPS(unsigned long now) {
  while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());
  if (now - lastGPSUpdate >= INTERVAL_GPS) {
    lastGPSUpdate = now;
    if (gps.location.isValid() && gps.location.isUpdated()) {
      lokasiSaatIni.latitude   = gps.location.lat();
      lokasiSaatIni.longitude  = gps.location.lng();
      lokasiSaatIni.altitude   = gps.altitude.isValid()   ? gps.altitude.meters() : 0.0;
      lokasiSaatIni.speed      = gps.speed.isValid()      ? gps.speed.kmph()      : 0.0;
      lokasiSaatIni.satellites = gps.satellites.isValid() ? gps.satellites.value(): 0;
      lokasiSaatIni.valid      = true;
      lokasiSaatIni.timestamp  = now;
      Serial.printf("[GPS] Lat:%.6f Lng:%.6f Alt:%.1fm Spd:%.1fkm/h Sat:%d\n",
        lokasiSaatIni.latitude, lokasiSaatIni.longitude,
        lokasiSaatIni.altitude, lokasiSaatIni.speed, lokasiSaatIni.satellites);
    } else {
      lokasiSaatIni.valid = false;
      Serial.printf("[GPS] Mencari sinyal... (chars: %lu)\n", gps.charsProcessed());
    }
  }
}
// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  nvs_flash_erase();
  nvs_flash_init();

  Serial.println("=========================================");
  Serial.println("   ESP32 #1 — MASTER (WiFi/GPS/Firebase)");
  Serial.println("=========================================");

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  delay(300);
  digitalWrite(BUZZER_PIN, HIGH);
  delay(300);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("[BUZZER] Siap");

  // GPS — UART2, RX=16, TX=17
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println("[GPS] UART2 dimulai");

  inisialisasiWiFi();
  if (internetOK) inisialisasiFirebase();

  Serial.println("-----------------------------------------");
  Serial.println("ESP32 #1 siap!");
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  unsigned long now = millis();
  bacaGPS(now);

  if (now - lastFirebaseSend >= INTERVAL_FIREBASE) {
    lastFirebaseSend = now;
    if (WiFi.status() != WL_CONNECTED) {
      kelolaReconnectWiFi(now);
    } else {
      wifiReconnecting = false;
      kirimGPSKeFirebase();
      if (buttonPending) kirimButtonKeFirebase(buttonPendingVal);
    }
  }
  if (wifiReconnecting) kelolaReconnectWiFi(now);

  kelolaBuzzer(now);
  kelolaButton(now);

  Serial.printf("[MASTER] GPS:%s | WiFi:%s | Btn:%s\n",
    lokasiSaatIni.valid           ? "Fix"   : "NoFix",
    WiFi.status() == WL_CONNECTED ? "OK"    : "PUTUS",
    buttonState                   ? "TEKAN" : "LEPAS");

  delay(200);
}