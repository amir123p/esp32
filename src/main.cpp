#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ChronosESP32.h>
#include "anim.h"


// ===== PROTOTYPE =====
void updateNavigationDisplay();
void connectionCallback(bool state);
void notificationCallback(Notification notification);
void configCallback(Config config, uint32_t a, uint32_t b);

// =====================================================
// ===== GLOBAL ========================================
// =====================================================

// ===== OLED =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_SDA_PIN 21
#define OLED_SCL_PIN 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ===== Chronos =====
ChronosESP32 watch("ESP_gw");

// ===== Button =====
#define BUTTON_PIN 23
#define BUTTON_ACTIVE_LOW true

// ===== Navigation State =====
bool change = false;
uint32_t nav_crc = 0xFFFFFFFF;
Navigation currentNavData;
bool isNavigationActive = false;

// ===== Idle Mode =====
enum IdleMode : uint8_t { IDLE_CLOCK = 0, IDLE_ANIM = 1 };
IdleMode idleMode = IDLE_CLOCK;

// ===== Timing =====
static unsigned long lastClockDraw = 0;
static unsigned long lastAnimDraw  = 0;
static uint8_t animIndex = 0;

// ===== Button Debounce =====
static unsigned long lastBtnChange = 0;
static bool lastBtnState = HIGH;

// ===== Notification Overlay =====
volatile bool notifActive = false;
static unsigned long notifUntil = 0;
static String notifApp, notifTitle, notifMsg;
static int notifScrollY = 0;
static unsigned long lastNotifScroll = 0;

#define NOTIF_SHOW_MS 6000
#define NOTIF_SCROLL_MS 180



// =====================================================
// ===== HELPER FUNCTIONS ==============================
// =====================================================

static void drawCenteredText(const String &text, int textSize, int y) {
  display.setTextSize(textSize);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int x = (SCREEN_WIDTH - (int)w) / 2;
  display.setCursor(x < 0 ? 0 : x, y);
  display.print(text);
}

static void drawWrappedText(const String &text, int x, int y, int maxW, int lineH, int maxLines) {
  String line = "";
  int lines = 0;

  for (int i = 0; i < (int)text.length();) {
    while (i < (int)text.length() && text[i] == ' ') i++;
    int j = i;
    while (j < (int)text.length() && text[j] != ' ') j++;
    String word = text.substring(i, j);
    i = j;

    if (!word.length()) continue;

    String test = line.length() ? (line + " " + word) : word;

    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(test, x, y + lines * lineH, &x1, &y1, &w, &h);

    if ((int)w > maxW && line.length()) {
      display.setCursor(x, y + lines * lineH);
      display.print(line);
      lines++;
      if (lines >= maxLines) return;
      line = word;
    } else {
      line = test;
    }
  }

  if (line.length() && lines < maxLines) {
    display.setCursor(x, y + lines * lineH);
    display.print(line);
  }
}

static void drawNotificationOverlay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("NOTIF: ");
  display.print(notifApp);

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  drawWrappedText(notifTitle, 0, 12, 128, 10, 2);

  int msgY = 34 - notifScrollY;
  drawWrappedText(notifMsg, 0, msgY, 128, 10, 30);

  display.display();
}

static void drawBigClock() {
  int hh = watch.getHourC();
  int mm = watch.getMinute();

  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText(String(buf), 3, 18);
  display.display();
}

static void drawAnimFrame(uint8_t idx) {
  if (ANIM_FRAME_COUNT == 0) return;
  if (idx >= ANIM_FRAME_COUNT) idx = 0;

  const uint8_t* frame = (const uint8_t*)pgm_read_ptr(&ANIM_FRAMES[idx]);

  display.clearDisplay();
  display.drawBitmap(0, 0, frame, 128, 64, SSD1306_WHITE);
  display.display();
}

static void drawIdle() {
  if (idleMode == IDLE_CLOCK) {
    if (millis() - lastClockDraw >= 400) {
      lastClockDraw = millis();
      drawBigClock();
    }
  } else {
    if (millis() - lastAnimDraw >= 90) {
      lastAnimDraw = millis();
      drawAnimFrame(animIndex++);
      if (animIndex >= ANIM_FRAME_COUNT) animIndex = 0;
    }
  }
}

static void handleButton() {
  bool raw = digitalRead(BUTTON_PIN);
  bool pressed = BUTTON_ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);

  if (millis() - lastBtnChange < 30) return;

  if (raw != lastBtnState) {
    lastBtnChange = millis();
    lastBtnState = raw;

    if (pressed) {
      idleMode = (idleMode == IDLE_CLOCK) ? IDLE_ANIM : IDLE_CLOCK;
      animIndex = 0;
      change = true;
    }
  }
}



// =====================================================
// ===== CALLBACK ======================================
// =====================================================

void connectionCallback(bool state) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Status: ");
  display.println(state ? "Connected" : "Disconnected");
  display.display();
}

void notificationCallback(Notification notification) {
  notifApp = notification.app;
  notifTitle = notification.title;
  notifMsg = notification.message;

  notifActive = true;
  notifUntil = millis() + NOTIF_SHOW_MS;
  notifScrollY = 0;
  lastNotifScroll = 0;
}

void configCallback(Config config, uint32_t a, uint32_t b) {
  // ===== CONFIG CALLBACK (diperbaiki) =====
  // Config adalah struct, coba akses field yang tersedia
  
  Serial.println("=== Config Callback Called ===");
  Serial.print("a: ");
  Serial.println(a);
  Serial.print("b: ");
  Serial.println(b);
  
  // Tampilkan di layar
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Config Updated!");
  display.display();
  delay(1000);
}



// =====================================================
// ===== UPDATE NAVIGATION DISPLAY (diperbaiki) =======
// =====================================================

void updateNavigationDisplay() {
  if (!isNavigationActive) return;
  
  currentNavData = watch.getNavigation();
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  
  // Cek apakah ada data (gunakan field yang tersedia)
  display.setCursor(0, 0);
  display.print("Nav Data:");
  
  // Latitude (gunakan field yang benar)
  display.setCursor(0, 12);
  display.print("Lat: ");
  display.println(currentNavData.lat, 6);  // ← ganti latitude jadi lat
  
  // Longitude
  display.setCursor(0, 24);
  display.print("Lon: ");
  display.println(currentNavData.lng, 6);  // ← ganti longitude jadi lng
  
  // Altitude (opsional)
  display.setCursor(0, 36);
  display.print("Alt: ");
  display.println(currentNavData.alt, 1);  // ← pakai alt
  
  // Course (arah)
  display.setCursor(0, 48);
  display.print("Course: ");
  display.println(currentNavData.course, 1);  // ← pakai course
  
  display.display();
}



// =====================================================
// ===== SETUP =========================================
// =====================================================

void setup() {
  Serial.begin(115200);
  
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("OLED not found!");
      for (;;) {}
    }
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  watch.setConnectionCallback(connectionCallback);
  watch.setNotificationCallback(notificationCallback);
  // watch.setConfigCallback(configCallback);  // ← Comment dulu kalau gak ada
  watch.begin();
  watch.setBattery(80);

  change = true;
  
  Serial.println("ESP32 Chronos Started!");
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("System Ready!");
  display.display();
  delay(1000);
}



// =====================================================
// ===== LOOP ==========================================
// =====================================================

void loop() {
  watch.loop();
  handleButton();

  // ===== NOTIFICATION PRIORITY =====
  if (notifActive) {

    if (millis() - lastNotifScroll > NOTIF_SCROLL_MS) {
      lastNotifScroll = millis();
      notifScrollY += 2;
      if (notifScrollY > 80) notifScrollY = 0;
    }

    drawNotificationOverlay();

    if ((long)(millis() - notifUntil) > 0) {
      notifActive = false;
      change = true;
    }

    delay(30);
    return;
  }

  // ===== NAV OR IDLE =====
  if (isNavigationActive) {
    currentNavData = watch.getNavigation();
    updateNavigationDisplay();
    delay(30);
    return;
  }

  drawIdle();
  delay(30);
}    display.setCursor(x, y + lines * lineH);
    display.print(line);
  }
}

static void drawNotificationOverlay() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("NOTIF: ");
  display.print(notifApp);

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  drawWrappedText(notifTitle, 0, 12, 128, 10, 2);

  int msgY = 34 - notifScrollY;
  drawWrappedText(notifMsg, 0, msgY, 128, 10, 30);

  display.display();
}

static void drawBigClock() {
  int hh = watch.getHourC();
  int mm = watch.getMinute();

  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", hh, mm);

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  drawCenteredText(String(buf), 3, 18);
  display.display();
}

static void drawAnimFrame(uint8_t idx) {
  if (ANIM_FRAME_COUNT == 0) return;
  if (idx >= ANIM_FRAME_COUNT) idx = 0;

  const uint8_t* frame = (const uint8_t*)pgm_read_ptr(&ANIM_FRAMES[idx]);

  display.clearDisplay();
  display.drawBitmap(0, 0, frame, 128, 64, SSD1306_WHITE);
  display.display();
}

static void drawIdle() {
  if (idleMode == IDLE_CLOCK) {
    if (millis() - lastClockDraw >= 400) {
      lastClockDraw = millis();
      drawBigClock();
    }
  } else {
    if (millis() - lastAnimDraw >= 90) {
      lastAnimDraw = millis();
      drawAnimFrame(animIndex++);
      if (animIndex >= ANIM_FRAME_COUNT) animIndex = 0;
    }
  }
}

static void handleButton() {
  bool raw = digitalRead(BUTTON_PIN);
  bool pressed = BUTTON_ACTIVE_LOW ? (raw == LOW) : (raw == HIGH);

  if (millis() - lastBtnChange < 30) return;

  if (raw != lastBtnState) {
    lastBtnChange = millis();
    lastBtnState = raw;

    if (pressed) {
      idleMode = (idleMode == IDLE_CLOCK) ? IDLE_ANIM : IDLE_CLOCK;
      animIndex = 0;
      change = true;
    }
  }
}



// =====================================================
// ===== CALLBACK ======================================
// =====================================================

void connectionCallback(bool state) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Status: ");
  display.println(state ? "Connected" : "Disconnected");
  display.display();
}

void notificationCallback(Notification notification) {
  notifApp = notification.app;
  notifTitle = notification.title;
  notifMsg = notification.message;

  notifActive = true;
  notifUntil = millis() + NOTIF_SHOW_MS;
  notifScrollY = 0;
  lastNotifScroll = 0;
}

void configCallback(Config config, uint32_t a, uint32_t b) {
  // ===== IMPLEMENTASI CONFIG CALLBACK =====
  // Fungsi ini dipanggil saat ada perubahan konfigurasi dari HP
  // Untuk sekarang, tampilkan pesan di serial
  
  Serial.println("=== Config Callback Called ===");
  Serial.print("Config: ");
  Serial.println(config.toString().c_str());
  Serial.print("a: ");
  Serial.println(a);
  Serial.print("b: ");
  Serial.println(b);
  
  // Tampilkan di layar (opsional)
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Config Updated!");
  display.display();
  delay(2000);
}



// =====================================================
// ===== UPDATE NAVIGATION DISPLAY ====================
// =====================================================

void updateNavigationDisplay() {
  // ===== IMPLEMENTASI NAVIGASI =====
  if (!isNavigationActive) return;
  
  currentNavData = watch.getNavigation();
  
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  
  // Status koneksi
  display.setCursor(0, 0);
  display.print("Nav: ");
  display.println(currentNavData.isValid ? "Active" : "Waiting");
  
  // Koordinat
  display.setCursor(0, 12);
  display.print("Lat: ");
  display.println(currentNavData.latitude, 6);
  
  display.setCursor(0, 24);
  display.print("Lon: ");
  display.println(currentNavData.longitude, 6);
  
  // Kecepatan & Arah (jika ada)
  display.setCursor(0, 36);
  display.print("Speed: ");
  display.print(currentNavData.speed, 1);
  display.println(" km/h");
  
  display.setCursor(0, 48);
  display.print("Heading: ");
  display.print(currentNavData.heading, 1);
  display.println(" deg");
  
  display.display();
}



// =====================================================
// ===== SETUP =========================================
// =====================================================

void setup() {
  Serial.begin(115200);
  
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("OLED not found!");
      for (;;) {}
    }
  }

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  watch.setConnectionCallback(connectionCallback);
  watch.setNotificationCallback(notificationCallback);
  watch.setConfigCallback(configCallback);  // ← Tambahkan ini!
  watch.begin();
  watch.setBattery(80);

  change = true;
  
  Serial.println("ESP32 Chronos Started!");
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("System Ready!");
  display.display();
  delay(1000);
}



// =====================================================
// ===== LOOP ==========================================
// =====================================================

void loop() {
  watch.loop();
  handleButton();

  // ===== NOTIFICATION PRIORITY =====
  if (notifActive) {

    if (millis() - lastNotifScroll > NOTIF_SCROLL_MS) {
      lastNotifScroll = millis();
      notifScrollY += 2;
      if (notifScrollY > 80) notifScrollY = 0;
    }

    drawNotificationOverlay();

    if ((long)(millis() - notifUntil) > 0) {
      notifActive = false;
      change = true;
    }

    delay(30);
    return;
  }

  // ===== NAV OR IDLE =====
  if (isNavigationActive) {
    currentNavData = watch.getNavigation();
    updateNavigationDisplay();
    delay(30);
    return;
  }

  drawIdle();
  delay(30);
}
