#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <ChronosESP32.h>
#include <ESP32Time.h>

// ===== OLED CONFIG =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

#define SDA_PIN 21
#define SCL_PIN 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== CHRONOS CONFIG =====
ChronosESP32 chronos("ESP32-NAV");
ESP32Time rtc;

// ===== UI STATE =====
bool lastConnected = false;

Notification lastNotif;
bool showingNotif = false;
unsigned long notifShownAt = 0;
const unsigned long NOTIF_SHOW_MS = 5000;

// ===== Helpers =====
static void clearAndBase() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
}

static void showStatus(const String &l1, const String &l2 = "") {
  clearAndBase();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(l1);
  if (l2.length()) display.println(l2);
  display.display();
}

static void drawWrapped(const String &textIn, int x, int y, int w, int maxLines) {
  String text = textIn;
  text.trim();
  int charsPerLine = max(1, w / 6); // ~6px per char at size 1
  int line = 0;
  int idx = 0;

  while (idx < (int)text.length() && line < maxLines) {
    int take = min(charsPerLine, (int)text.length() - idx);
    int cut = idx + take;

    if (cut < (int)text.length()) {
      int lastSpace = text.lastIndexOf(' ', cut);
      if (lastSpace > idx) cut = lastSpace;
    }

    display.setCursor(x, y + line * 10);
    display.print(text.substring(idx, cut));

    idx = cut;
    while (idx < (int)text.length() && text[idx] == ' ') idx++;
    line++;
  }
}

static String trimShort(String s, int maxLen) {
  s.trim();
  if ((int)s.length() > maxLen) s = s.substring(0, maxLen);
  return s;
}

static bool notifLooksValid(const Notification &n) {
  return (n.app.length() || n.title.length() || n.message.length());
}

static void drawBigCenteredTime() {
  // ambil jam dari ChronosESP32 (dia extends ESP32Time)
  // pastikan chronos.loop() jalan biar time ke-update dari app
  String hh = chronos.getHourZ();   // zero padded
  int mm = chronos.getMinute();
  char buf[6];
  snprintf(buf, sizeof(buf), "%s:%02d", hh.c_str(), mm);
  String t = String(buf);

  // maksimal buat OLED 128x64: textSize 3 masih muat pas-pasan
  // size 4 kebanyakan, kecuali font custom (lo belum siap)
  display.setTextSize(3);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(t, 0, 0, &x1, &y1, &w, &h);

  int x = (SCREEN_WIDTH - (int)w) / 2;
  int y = 14; // biar tengah

  display.setCursor(max(0, x), y);
  display.print(t);
}

static void drawWeatherSmall() {
  int wc = chronos.getWeatherCount();
  if (wc <= 0) return;

  Weather w = chronos.getWeatherAt(0);

  display.setTextSize(1);
  display.setCursor(0, 54);
  display.print(w.temp);
  display.print("C ");
  display.print(w.high);
  display.print("/");
  display.print(w.low);

  // kota kalau mau (tapi kepanjangan biasanya)
  // display.setCursor(80, 54);
  // display.print(trimShort(chronos.getWeatherCity(), 8));
}

static void drawIdleScreen() {
  clearAndBase();

  // kalau time belum keset, tampilkan “--:--”
  // tapi kita tetap coba gambar jam
  drawBigCenteredTime();
  drawWeatherSmall();

  display.display();
}

static void drawNotifScreen() {
  clearAndBase();
  display.setTextSize(1);

  String app = trimShort(lastNotif.app, 20);
  String title = trimShort(lastNotif.title, 26);
  String msg = lastNotif.message;
  msg.trim();

  display.setCursor(0, 0);
  display.println("NOTIF:");
  display.println(app.length() ? app : "-");

  display.setCursor(0, 22);
  drawWrapped(title.length() ? title : "-", 0, 22, 128, 2);

  display.setCursor(0, 44);
  drawWrapped(msg.length() ? msg : "-", 0, 44, 128, 2);

  display.display();
}

static void drawNavScreen(const Navigation &nav) {
  clearAndBase();

  // PANAH / ICON 48x48 dari Chronos
  // Icon ini 1bpp, cocok buat SSD1306
  if (nav.hasIcon) {
    display.drawBitmap(
      40,   // (128-48)/2 = 40 biar tengah
      0,
      nav.icon,
      48,
      48,
      SSD1306_WHITE
    );
  } else {
    // kalau belum ada icon, kasih teks biar ga kosong
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("NAV...");
  }

  // Info teks bawah
  display.setTextSize(1);

  // jarak/title biasanya “850 m” di title, atau jarak ke next step
  String top = trimShort(nav.title, 20);
  display.setCursor(0, 48);
  display.print(top.length() ? top : "-");

  String instr = nav.directions; instr.trim();
  if (instr.length() == 0) instr = "Next";

  // instruksi 1 baris aja (OLED kecil)
  display.setCursor(0, 56);
  display.print(trimShort(instr, 21));

  display.display();
}

void setup() {
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (true) delay(1000);
  }

  showStatus("ESP32 NAV", "Waiting Chronos...");
  chronos.begin();
}

void loop() {
  chronos.loop();

  bool connected = chronos.isConnected();
  if (connected != lastConnected) {
    lastConnected = connected;
    showStatus(connected ? "Connected" : "Disconnected",
               connected ? "Open Chronos app" : "Pair again");
  }

  if (!connected) {
    clearAndBase();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Not connected.");
    display.println("Pair in Chronos.");
    display.println("Device: ESP32-NAV");
    display.display();
    delay(200);
    return;
  }

  // ===== NOTIF MODE =====
  int nCount = chronos.getNotificationCount();
  if (!showingNotif && nCount > 0) {
    Notification notif = chronos.getNotificationAt(nCount - 1);
    if (notifLooksValid(notif)) {
      lastNotif = notif;
      showingNotif = true;
      notifShownAt = millis();
      chronos.clearNotifications(); // biar gak spam notif yang sama
    }
  }

  if (showingNotif) {
    drawNotifScreen();
    if (millis() - notifShownAt > NOTIF_SHOW_MS) showingNotif = false;
    delay(120);
    return;
  }

  // ===== NAV MODE =====
  Navigation nav = chronos.getNavigation();

  // nav.active = navigasi jalan
  // nav.isNavigation = kadang dipakai untuk general nav info
  if (nav.active || nav.isNavigation) {
    drawNavScreen(nav);
    delay(120);
    return;
  }

  // ===== IDLE MODE =====
  drawIdleScreen();
  delay(250);
}
