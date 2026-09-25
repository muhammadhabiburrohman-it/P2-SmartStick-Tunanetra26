// ============================================================
//  secrets.example.h
//  Salin file ini menjadi "secrets.h" lalu isi dengan
//  kredensial WiFi & Firebase milikmu sendiri.
//
//  secrets.h TIDAK boleh di-commit ke Git (sudah masuk .gitignore).
// ============================================================
#pragma once

// ─── WiFi ────────────────────────────────────────────────────
const char* WIFI_SSID     = "NAMA_WIFI_KAMU";
const char* WIFI_PASSWORD = "PASSWORD_WIFI_KAMU";

// ─── Firebase Realtime Database ─────────────────────────────
// Dapatkan dari Firebase Console > Project Settings > Service Accounts
// / Realtime Database > Rules (Database Secret untuk legacy token)
#define FIREBASE_HOST "https://NAMA-PROJECT-KAMU-default-rtdb.REGION.firebasedatabase.app/"
#define FIREBASE_AUTH "ISI_DENGAN_DATABASE_SECRET_ATAU_TOKEN_KAMU"
