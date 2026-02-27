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
ChronosESP32 chronos("ESP32");
ESP32Time rtc;

// ===== UI STATE =====
bool lastConnected = false;

Notification lastNotif;
bool showingNotif = false;
unsigned long notifShownAt = 0;
const unsigned long NOTIF_SHOW_MS = 5000;

bool timeSynced = false;

// ===== Helpers =====
static void showStatus(const String &l1, const String &l2 = "")
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(l1);
  if (l2.length())
    display.println(l2);
  display.display();
}

// Wrap teks biar muat OLED kecil
static void drawWrapped(const String &textIn, int x, int y, int w, int maxLines)
{
  String text = textIn;
  text.trim();

  int charsPerLine = max(1, w / 6); // kira2 6px/char untuk size 1
  int line = 0;
  int idx = 0;

  while (idx < (int)text.length() && line < maxLines)
  {
    int take = min(charsPerLine, (int)text.length() - idx);
    int cut = idx + take;

    if (cut < (int)text.length())
    {
      int lastSpace = text.lastIndexOf(' ', cut);
      if (lastSpace > idx)
        cut = lastSpace;
    }

    display.setCursor(x, y + line * 10);
    display.print(text.substring(idx, cut));

    idx = cut;
    while (idx < (int)text.length() && text[idx] == ' ')
      idx++;
    line++;
  }
}

static String trimShort(String s, int maxLen)
{
  s.trim();
  if ((int)s.length() > maxLen)
    s = s.substring(0, maxLen);
  return s;
}

static bool notifLooksValid(const Notification &n)
{
  return (n.app.length() || n.title.length() || n.message.length());
}

static void drawBigCenteredTime()
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if (!timeSynced)
  {
    display.setTextSize(1);
    display.setCursor(10, 28);
    display.println("Waiting time sync");
    display.display();
    return;
  }

  String now = rtc.getTime("%H:%M");
  display.setTextSize(3);

  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(now, 0, 0, &x1, &y1, &w, &h);

  int cx = (SCREEN_WIDTH - (int)w) / 2;
  int cy = (SCREEN_HEIGHT - (int)h) / 2;

  display.setCursor(cx, cy);
  display.print(now);

  display.display();
}

static void drawNotifScreen(const Notification &n)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  String app = trimShort(n.app, 20);
  String title = trimShort(n.title, 26);
  String msg = n.message;
  msg.trim();

  // header (app + jam kecil)
  display.setCursor(0, 0);
  display.println("NOTIF:");
  display.println(app.length() ? app : "-");

  // title
  drawWrapped(title.length() ? title : "-", 0, 22, 128, 2);

  // msg
  drawWrapped(msg.length() ? msg : "-", 0, 44, 128, 2);

  display.display();
}

static void drawNavScreen(const Navigation &nav)
{
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  // jarak step/title (gede)
  String dist = trimShort(nav.title, 6);
  String instr = nav.directions;
  instr.trim();
  String eta = nav.duration;
  eta.trim();
  if (!instr.length())
    instr = "Next step";

  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(dist.length() ? dist : "--");

  display.setTextSize(1);
  drawWrapped(instr, 0, 22, 128, 3);

  // ETA + jam kecil
  display.setCursor(0, 56);
  display.print("ETA: ");
  display.print(eta.length() ? eta : "-");

  if (timeSynced)
  {
    String now = rtc.getTime("%H:%M");
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(now, 0, 0, &x1, &y1, &w, &h);
    display.setCursor(SCREEN_WIDTH - w, 56);
    display.print(now);
  }

  display.display();
}

// ===== Chronos callbacks =====
// Waktu sync paling aman lewat configuration callback (CF_TIME)
static void onConfig(Config cfg, uint32_t a, uint32_t b)
{
  // Banyak versi Chronos ngirim time via CF_TIME.
  // Biasanya a = epoch atau komponen waktu. Kita coba interpretasi paling umum: epoch seconds di 'a'
  if (cfg == CF_TIME)
  {
    // Kalau a masuk akal sebagai epoch (>= 1.6B itu sekitar 2020+)
    if (a > 1600000000UL)
    {
      rtc.setTime((time_t)a);
      timeSynced = true;
    }
    else
    {
      // fallback: kalau ternyata bukan epoch, minimal tandain sudah ada sync
      // (kalo mau bener2 akurat, harus cek format asli di library/packet)
      timeSynced = true;
    }
  }
}

void setup()
{
  Serial.begin(115200);

  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR))
  {
    while (true)
      delay(1000);
  }

  showStatus("ESP32 NAV", "Waiting Chronos...");

  chronos.setConfigurationCallback(onConfig);
  chronos.begin();
}

void loop()
{
  chronos.loop();

  bool connected = chronos.isConnected();
  if (connected != lastConnected)
  {
    lastConnected = connected;
    showStatus(connected ? "Connected" : "Disconnected",
               connected ? "Open Chronos app" : "Pair again");

    // reset state
    showingNotif = false;
  }

  // belum connect
  if (!connected)
  {
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

  // ===== NOTIF MODE =====
  int nCount = chronos.getNotificationCount();
  if (!showingNotif && nCount > 0)
  {
    // notif terakhir = index nCount-1 (sesuai API chronos-esp32 1.7.0)
    Notification notif = chronos.getNotificationAt(nCount - 1);

    if (notifLooksValid(notif))
    {
      lastNotif = notif;
      showingNotif = true;
      notifShownAt = millis();

      // biar gak spam notif sama terus
      chronos.clearNotifications();
    }
  }

  if (showingNotif)
  {
    drawNotifScreen(lastNotif);

    if (millis() - notifShownAt > NOTIF_SHOW_MS)
      showingNotif = false;

    delay(120);
    return;
  }

  // ===== NAV MODE =====
  Navigation nav = chronos.getNavigation();
  if (nav.active)
  {
    drawNavScreen(nav);
    delay(120);
    return;
  }

  // ===== IDLE MODE (jam gede tengah) =====
  drawBigCenteredTime();
  delay(200);
}
