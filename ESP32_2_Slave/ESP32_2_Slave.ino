#include <Arduino.h>
#include "AudioTools.h"
#include "BluetoothA2DPSource.h"
#include "suara_air.h"
#include "suara_depan.h"
#include "suara_kiri.h"
#include "suara_kanan.h"

// ───────────────────────────── PIN SENSOR ─────────────────────────────
#define TRIG_DepanBawah   5
#define ECHO_DepanBawah   18
#define TRIG_DepanTengah  26
#define ECHO_DepanTengah  27

#define TRIG_Kiri   32
#define ECHO_Kiri   33
#define TRIG_Kanan  19
#define ECHO_Kanan  21

#define WATER_PIN   34
#define MOTOR_PIN   25

// ───────────────────────── PWM MOTOR ───────────────────────────
#define PWM_FREQ      1000
#define PWM_RES       8
#define MOTOR_MATI    0
#define MOTOR_NYALA   255   // on/off saja, tidak ada level bertingkat

// ────────────────── BATAS JARAK PER ARAH (cm) ───────────────────
#define BATAS_DEPAN_MASUK   120
#define BATAS_DEPAN_KELUAR  126
#define BATAS_KIRI_MASUK    40
#define BATAS_KIRI_KELUAR   46
#define BATAS_KANAN_MASUK   40
#define BATAS_KANAN_KELUAR  46

// ────────────────────── AUDIO GAIN (VOLUME) ─────────────────────
#define AUDIO_GAIN_FIXED  896

// ───────────────────── THRESHOLD & INTERVAL ────────────────────
#define WATER_THRESHOLD      500
#define WAV_HEADER_SIZE      44
#define INTERVAL_ULANG_AUDIO 2500

// ───────────────────────── OBJEK BT ────────────────────────────
BluetoothA2DPSource a2dp_source;

// ───────────────────── MODE / ANTRIAN AUDIO ─────────────────────
enum ModeSuara {
  MODE_NONE   = 0,
  MODE_DEPAN  = 1,
  MODE_KIRI   = 2,
  MODE_KANAN  = 3,
  MODE_AIR    = 4
};

#define ANTRIAN_MAKS  8
uint8_t       antrianMode[ANTRIAN_MAKS];
volatile int  antrianDepan    = 0;
volatile int  antrianBelakang = 0;

volatile bool     gAudioAktif  = false;
volatile uint32_t gAudioPosisi = WAV_HEADER_SIZE;
volatile uint32_t gAudioLen    = 0;
const uint8_t    *gAudioData   = nullptr;
portMUX_TYPE      audioMux     = portMUX_INITIALIZER_UNLOCKED;

// ─────────────────────── STATE SENSOR/BT ───────────────────────
bool depanTengahAktif = false;
bool depanBawahAktif  = false;
bool depanAktif       = false;  // gabungan tengah || bawah
bool kiriAktif        = false;
bool kananAktif       = false;
volatile bool adaAir      = false;
volatile bool btTerhubung = false;

bool statusHysDepanTengah = false;
bool statusHysDepanBawah  = false;
bool statusHysKiri        = false;
bool statusHysKanan       = false;

// ─────────────────────────── TIMING ────────────────────────────
unsigned long lastPlayCombo = 0;
uint32_t      comboSebelumnya = 0;

// =================================================================
//  AUDIO — pemetaan file per mode
// =================================================================

void ambilDataAudio(uint8_t mode, const uint8_t *&data, uint32_t &len) {
  switch (mode) {
    case MODE_DEPAN: data = suara_depan; len = suara_depan_len; break;
    case MODE_KIRI:  data = suara_kiri;  len = suara_kiri_len;  break;
    case MODE_KANAN: data = suara_kanan; len = suara_kanan_len; break;
    case MODE_AIR:   data = suara_air;   len = suara_air_len;   break;
    default:         data = nullptr;     len = 0;               break;
  }
}

void IRAM_ATTR muatAudioDariAntrian() {
  if (antrianDepan == antrianBelakang) {
    gAudioAktif = false;
    return;
  }
  uint8_t mode = antrianMode[antrianDepan];
  antrianDepan = (antrianDepan + 1) % ANTRIAN_MAKS;

  const uint8_t *data; uint32_t len;
  ambilDataAudio(mode, data, len);

  if (data == nullptr || len == 0) {
    gAudioAktif = false;
    return;
  }
  gAudioData   = data;
  gAudioLen    = len;
  gAudioPosisi = WAV_HEADER_SIZE;
  gAudioAktif  = true;
}

void tambahAntrian(uint8_t mode) {
  portENTER_CRITICAL(&audioMux);

  int next = (antrianBelakang + 1) % ANTRIAN_MAKS;
  if (next != antrianDepan) {
    antrianMode[antrianBelakang] = mode;
    antrianBelakang = next;
  }

  if (!gAudioAktif) {
    muatAudioDariAntrian();
  }

  portEXIT_CRITICAL(&audioMux);
}

int32_t audioCallback(uint8_t *output, int32_t byteCount) {
  bool adaDataAudio = false;

  portENTER_CRITICAL_ISR(&audioMux);
  if (gAudioAktif && gAudioData) {
    uint32_t sisa  = gAudioLen - gAudioPosisi;
    uint32_t ambil = min((uint32_t)byteCount, sisa);

    memcpy(output, gAudioData + gAudioPosisi, ambil);
    gAudioPosisi += ambil;

    if (ambil < (uint32_t)byteCount)
      memset(output + ambil, 0, byteCount - ambil);

    adaDataAudio = true;

    if (gAudioPosisi >= gAudioLen) {
      muatAudioDariAntrian();
    }
  } else {
    memset(output, 0, byteCount);
  }
  portEXIT_CRITICAL_ISR(&audioMux);

  if (adaDataAudio) {
    int16_t *samples      = (int16_t*)output;
    int32_t  jumlahSample = byteCount / 2;
    for (int32_t i = 0; i < jumlahSample; i++) {
      int32_t nilaiBaru = (samples[i] * AUDIO_GAIN_FIXED) >> 8;
      if (nilaiBaru > 32767)  nilaiBaru = 32767;
      if (nilaiBaru < -32768) nilaiBaru = -32768;
      samples[i] = (int16_t)nilaiBaru;
    }
  }

  return byteCount;
}

void koneksiCallback(esp_a2d_connection_state_t state, void *ptr) {
  btTerhubung = (state == ESP_A2D_CONNECTION_STATE_CONNECTED);
}

// =================================================================
//  LOGIKA AUDIO: depan / kiri / kanan / air -> antrian suara
// =================================================================
void kelolaAudio(unsigned long now) {
  if (!btTerhubung) return;

  bool modeDiminta[5] = {false, false, false, false, false};

  if (depanAktif) modeDiminta[MODE_DEPAN] = true;
  if (kiriAktif)  modeDiminta[MODE_KIRI]  = true;
  if (kananAktif) modeDiminta[MODE_KANAN] = true;
  if (adaAir)     modeDiminta[MODE_AIR]   = true;

  uint32_t comboSaatIni =
      (depanAktif << 0) | (kiriAktif << 1) | (kananAktif << 2) | (adaAir << 3);

  bool adaPermintaan = modeDiminta[MODE_DEPAN] || modeDiminta[MODE_KIRI] ||
                        modeDiminta[MODE_KANAN] || modeDiminta[MODE_AIR];

  if (!adaPermintaan) {
    comboSebelumnya = 0;
    return;
  }

  bool comboBerubah  = (comboSaatIni != comboSebelumnya);
  bool waktunyaUlang  = (now - lastPlayCombo >= INTERVAL_ULANG_AUDIO);

  if (comboBerubah || waktunyaUlang) {
    lastPlayCombo   = now;
    comboSebelumnya = comboSaatIni;

    if (modeDiminta[MODE_DEPAN]) tambahAntrian(MODE_DEPAN);
    if (modeDiminta[MODE_KIRI])  tambahAntrian(MODE_KIRI);
    if (modeDiminta[MODE_KANAN]) tambahAntrian(MODE_KANAN);
    if (modeDiminta[MODE_AIR])   tambahAntrian(MODE_AIR);
  }
}

// =================================================================
//  SENSOR
// =================================================================

long bacaJarakSekali(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long d = pulseIn(echoPin, HIGH, 15000);
  return d ? (long)(d * 0.034f / 2.0f) : -1;
}

long bacaJarak(int trigPin, int echoPin) {
  long sampel[5];
  for (int i = 0; i < 5; i++) {
    long v = bacaJarakSekali(trigPin, echoPin);
    sampel[i] = (v == -1) ? 999 : v;
    delayMicroseconds(600);
  }
  for (int i = 1; i < 5; i++) {
    long key = sampel[i];
    int j = i - 1;
    while (j >= 0 && sampel[j] > key) {
      sampel[j + 1] = sampel[j];
      j--;
    }
    sampel[j + 1] = key;
  }
  long median = sampel[2];
  return (median >= 999) ? -1 : median;
}

// Deteksi biner dengan hysteresis
bool deteksiHys(long jarak, int batasMasuk, int batasKeluar, bool statusSebelumnya) {
  if (jarak == -1) return false;

  if (statusSebelumnya) {
    return (jarak <= batasKeluar);
  } else {
    return (jarak <= batasMasuk);
  }
}

// =================================================================
//  SETUP & LOOP
// =================================================================

void setup() {
  Serial.begin(115200);

  pinMode(TRIG_DepanBawah, OUTPUT);  pinMode(ECHO_DepanBawah, INPUT);
  pinMode(TRIG_DepanTengah, OUTPUT); pinMode(ECHO_DepanTengah, INPUT);
  pinMode(TRIG_Kiri, OUTPUT);        pinMode(ECHO_Kiri, INPUT);
  pinMode(TRIG_Kanan, OUTPUT);       pinMode(ECHO_Kanan, INPUT);
  pinMode(WATER_PIN, INPUT);

  ledcAttach(MOTOR_PIN, PWM_FREQ, PWM_RES);
  ledcWrite(MOTOR_PIN, MOTOR_MATI);

  ledcWrite(MOTOR_PIN, MOTOR_NYALA);
  delay(300);
  ledcWrite(MOTOR_PIN, MOTOR_MATI);

  a2dp_source.set_on_connection_state_changed(koneksiCallback);
  a2dp_source.set_data_callback(audioCallback);
  a2dp_source.start("Bluetooth music");
  a2dp_source.set_volume(127);

  Serial.println("ESP32 Siap — Mencari BT...");
}

void loop() {
  unsigned long now = millis();

  // Depan: delay 150ms (2 sensor searah, rawan crosstalk tinggi)
  long jDepanBawah  = bacaJarak(TRIG_DepanBawah, ECHO_DepanBawah);
  delay(150);
  long jDepanTengah = bacaJarak(TRIG_DepanTengah, ECHO_DepanTengah);
  delay(150);

  // Kiri & Kanan: delay 60ms sesuai spesifikasi sensor
  long jKiri  = bacaJarak(TRIG_Kiri, ECHO_Kiri);
  delay(60);
  long jKanan = bacaJarak(TRIG_Kanan, ECHO_Kanan);
  delay(60);

  // Hysteresis per sensor
  depanBawahAktif  = deteksiHys(jDepanBawah,  BATAS_DEPAN_MASUK, BATAS_DEPAN_KELUAR, statusHysDepanBawah);
  statusHysDepanBawah = depanBawahAktif;

  depanTengahAktif = deteksiHys(jDepanTengah, BATAS_DEPAN_MASUK, BATAS_DEPAN_KELUAR, statusHysDepanTengah);
  statusHysDepanTengah = depanTengahAktif;

  kiriAktif = deteksiHys(jKiri, BATAS_KIRI_MASUK, BATAS_KIRI_KELUAR, statusHysKiri);
  statusHysKiri = kiriAktif;

  kananAktif = deteksiHys(jKanan, BATAS_KANAN_MASUK, BATAS_KANAN_KELUAR, statusHysKanan);
  statusHysKanan = kananAktif;

  // Depan = gabungan Tengah ATAU Bawah (salah satu terdeteksi ≤120cm sudah cukup)
  depanAktif = depanTengahAktif || depanBawahAktif;

  // Motor getar: nyala HANYA kalau depan aktif sendirian (kiri/kanan tidak ikut aktif)
  bool getarAktif = depanAktif;
  ledcWrite(MOTOR_PIN, getarAktif ? MOTOR_NYALA : MOTOR_MATI);

  adaAir = (analogRead(WATER_PIN) > WATER_THRESHOLD);

  kelolaAudio(now);

  static unsigned long lastLog = 0;
  if (now - lastLog >= 500) {
    lastLog = now;
    Serial.printf("DepanBawah:%ldcm[%s] | DepanTengah:%ldcm[%s] | Kiri:%ldcm[%s] | Kanan:%ldcm[%s] | Air:%s | BT:%s\n",
      jDepanBawah,  depanBawahAktif  ? "AKTIF" : "aman",
      jDepanTengah, depanTengahAktif ? "AKTIF" : "aman",
      jKiri,  kiriAktif  ? "AKTIF" : "aman",
      jKanan, kananAktif ? "AKTIF" : "aman",
      adaAir      ? "ADA" : "Aman",
      btTerhubung ? "OK"  : "..."
    );
  }
}