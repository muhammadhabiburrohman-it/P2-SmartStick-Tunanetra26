# BiFi Smart Stick

Tongkat pintar (smart cane) untuk membantu penyandang tunanetra mendeteksi halangan di sekitar, mengirimkan lokasi ke keluarga, serta memberikan peringatan darurat saat kontak dengan air — dilengkapi konektivitas internet mandiri melalui MiFi.

## Fitur Utama

- **Deteksi Objek Multi-Arah**
  Mendeteksi halangan di depan (jarak hingga 100 cm) serta kanan dan kiri (jarak hingga 50 cm). Jika jarak objek kurang dari ambang batas tersebut, sistem akan memberi tahu arah objek (depan/kanan/kiri) melalui suara pada TWS (True Wireless Stereo).

- **Peringatan Getar (Vibration Motor)**
  Motor getar akan aktif apabila kedua sensor ultrasonik depan (atas dan bawah) mendeteksi halangan secara bersamaan, memberi sinyal peringatan fisik tambahan selain suara.

- **Pelacakan Lokasi via WiFi**
  ESP32 pertama bertugas mengirimkan data lokasi pengguna secara real-time melalui koneksi WiFi.

- **Notifikasi Darurat via WhatsApp**
  ESP32 kedua mengaktifkan Bluetooth yang terhubung ke TWS untuk audio, serta mengirimkan notifikasi ke nomor WhatsApp keluarga sebagai bentuk pemantauan jarak jauh.

- **Deteksi Air (Water Level Sensor)**
  Jika tongkat terkena air, sensor water level akan mendeteksinya dan sistem akan memberikan peringatan suara kepada pengguna.

- **Konektivitas Internet Mandiri**
  Menggunakan MiFi sebagai sumber koneksi internet agar pengiriman data lokasi dan notifikasi WhatsApp tetap berjalan tanpa bergantung pada jaringan WiFi tetap.

- **Indikator Koneksi (Buzzer)**
  Jika internet tidak terhubung, buzzer (tipe *active buzzer*) akan berbunyi sebagai peringatan bahwa fitur pelacakan lokasi dan notifikasi darurat sedang tidak aktif.

## Komponen Perangkat Keras

| Komponen | Jumlah | Fungsi |
|---|---|---|
| ESP32 Dev Kit (#1) | 1 | Mengelola koneksi WiFi & pengiriman data lokasi |
| ESP32 Dev Kit (#2) | 1 | Mengelola Bluetooth (TWS) & notifikasi WhatsApp |
| Sensor Ultrasonik (HC-SR04) | 4 | Deteksi objek: kanan, kiri, depan bawah, depan atas |
| Motor Getar (Vibration Motor) | 1 | Peringatan fisik saat halangan depan terdeteksi |
| Sensor Water Level | 1 | Deteksi kontak dengan air |
| Buzzer (Active) | 1 | Indikator internet tidak terhubung |
| TWS (True Wireless Stereo) | 1 pasang | Output suara peringatan arah objek & notifikasi |
| MiFi / Modem Portable | 1 | Sumber koneksi internet |

## Cara Kerja Singkat

1. Empat sensor ultrasonik secara terus-menerus memindai jarak di empat arah: kanan, kiri, depan bawah, dan depan atas.
2. Jika jarak depan < 100 cm, atau jarak kanan/kiri < 50 cm, sistem mengirim peringatan suara arah objek ke TWS.
3. Jika kedua sensor depan (atas & bawah) mendeteksi halangan secara bersamaan, motor getar aktif sebagai peringatan tambahan.
4. ESP32 #1 mengirimkan data lokasi pengguna secara berkala melalui WiFi (via MiFi).
5. ESP32 #2 menjaga koneksi Bluetooth ke TWS dan mengirimkan notifikasi ke nomor WhatsApp keluarga.
6. Jika sensor water level mendeteksi air, sistem memicu peringatan suara ke pengguna.
7. Jika koneksi internet terputus, buzzer active berbunyi sebagai indikator bahwa fitur lokasi & notifikasi WA sedang tidak berfungsi.

## Status Proyek

Proyek ini merupakan bagian dari tugas akhir (skripsi) pengembangan tongkat pintar berbasis ESP32 untuk penyandang tunanetra.

## Lisensi

Tambahkan lisensi sesuai kebutuhan (misalnya MIT License).
