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
static void showStatus(const String &l1, const String &l2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
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
  // ChronosESP32 v1.7.0 Notification ga punya "active"
  // Jadi kita anggap valid kalau ada isi yang masuk akal
  return (n.app.length() || n.title.length() || n.message.length());
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
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Not connected.");
    display.println("Pair in Chronos.");
    display.println("Device: ESP32-NAV");
    display.display();
    delay(200);
    return;
  }

  // ===== NOTIFICATION MODE (ChronosESP32 v1.7.0 API) =====
  // Ambil notif terakhir kalau ada
  int nCount = chronos.getNotificationCount();
  if (!showingNotif && nCount > 0) {
    Notification notif = chronos.getNotificationAt(nCount - 1);

    if (notifLooksValid(notif)) {
      lastNotif = notif;
      showingNotif = true;
      notifShownAt = millis();

      // optional: biar notif yang sama nggak muncul ulang terus
      chronos.clearNotifications();
    }
  }

  if (showingNotif) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);

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

    if (millis() - notifShownAt > NOTIF_SHOW_MS) {
      showingNotif = false;
    }

    delay(120);
    return;
  }

  // ===== NAVIGATION MODE =====
  Navigation nav = chronos.getNavigation();

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (!nav.active) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Connected.");
    display.println("Start Google Maps");
    display.println("navigation...");
    display.display();
    delay(200);
    return;
  }

  String dist = trimShort(nav.title, 6);
  String instr = nav.directions; instr.trim();
  String eta = nav.duration; eta.trim();
  if (instr.length() == 0) instr = "Next step";

  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(dist.length() ? dist : "--");

  display.setTextSize(1);
  drawWrapped(instr, 0, 22, 128, 3);

  display.setCursor(0, 56);
  display.print("ETA: ");
  display.print(eta.length() ? eta : "-");

  display.display();
  delay(120);
}
