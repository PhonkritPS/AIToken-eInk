#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

// ไลบรารีสำหรับกราฟิกและจอ E-ink
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>

// =========================================================================
// กำหนดขาต่อสาย SPI สำหรับ NodeMCU V3 (ESP8266)
// =========================================================================
#ifndef D8
  #define D0 16
  #define D1 5
  #define D2 4
  #define D3 0
  #define D4 2
  #define D5 14
  #define D6 12
  #define D7 13
  #define D8 15
#endif

#define EPD_CS   D8 // GPIO15
#define EPD_DC   D3 // GPIO0
#define EPD_RST  D4 // GPIO2
#define EPD_BUSY D2 // GPIO4

// =========================================================================
// เลือกรุ่นหน้าจอ E-ink (ปิดโหมด 3 สี เพื่อใช้จอ ขาว-ดำ รุ่นใหม่)
// =========================================================================
//#define USE_3COLOR_DISPLAY  // <--- คอมเมนต์บรรทัดนี้ไว้เพื่อใช้จอ ขาว-ดำ

#ifndef GxEPD_RED
  #define GxEPD_RED 0xF800 // แม้เป็นจอขาว-ดำ ก็ประกาศเผื่อไว้กัน Error จากฟังก์ชันที่เรียกใช้สีแดง
#endif

#ifdef USE_3COLOR_DISPLAY
  #include <GxEPD2_3C.h>
  // จอ 2.9" 3 สี (Black / White / Red) รหัส ZJYE290S08ROG01 (GDEM029C90)
  GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT> display(GxEPD2_290_C90c(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
#else
  #include <GxEPD2_BW.h>
  // จอ 2.9" ขาว-ดำ สำหรับสายแพร E029A01 (E029A01-FPCA-V2.0 / E029A01N16C810 -> ชิป SSD1608 / GDEH029A1)
  GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT> display(GxEPD2_290(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
#endif

// Wi-Fi Config
const char *ssid = "IT-TEST";
const char *password = "ilovephonkrit";

// API Config
const char *apiUrl = "http://172.100.2.122:5000/api/quota";
const unsigned long refreshInterval = 60000; // เช็ค API ทุก 1 นาที
unsigned long lastFetchTime = 0;

// ตัวแปรเก็บค่าเดิม เพื่อเช็คว่าข้อมูลเปลี่ยนหรือไม่
int prevGeminiWeekly = -1;
int prevGemini5Hr    = -1;
int prevClaudeWeekly = -1;
int prevClaude5Hr    = -1;
String prevGwTime    = "";
String prevG5Time    = "";
String prevCwTime    = "";
String prevC5Time    = "";
bool isFirstRender   = true;

// Prototype functions
void fetchAndDisplayQuota();
void updateEinkDisplay(int gw, int g5, int cw, int c5, String gwSub, String g5Sub, String cwSub, String c5Sub);
void updateSingleBarPartial(int y, int h, int percent, const char* label, const char* sub);
void drawProgressBar(int x, int y, int w, int h, int percent, const char* label, const char* sub);
void drawBootScreen(const char* text);
void drawWiFiIconEink(int x, int y);

void setup() {
  Serial.begin(115200);

  // 1. เริ่มการทำงานจอ E-ink
  display.init(115200); 
  display.setRotation(1); // แนวนอน 296 x 128
  display.setTextColor(GxEPD_BLACK);

  // 2. แสดงหน้าจอเริ่มต้น
  drawBootScreen("Connecting to Wi-Fi...");

  // 3. เชื่อมต่อ Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    String mac = WiFi.macAddress();
    Serial.println("\nWiFi Connected! MAC: " + mac);
    drawBootScreen("WiFi Connected!\nFetching Data...");
    fetchAndDisplayQuota(); // โหลดข้อมูลทันที
  } else {
    drawBootScreen("WiFi Failed.\nPlease Check SSID/Pass");
  }
}

void loop() {
  // วนลูปเช็คเวลาดึงข้อมูล API
  if (millis() - lastFetchTime >= refreshInterval) {
    lastFetchTime = millis();
    fetchAndDisplayQuota();
  }
}

// =========================================================================
// ฟังก์ชันยิง API ดึงข้อมูลและตรวจสอบการเปลี่ยนแปลง
// =========================================================================
void fetchAndDisplayQuota() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
    return;
  }

  WiFiClient client;
  HTTPClient http;
  
  http.begin(client, apiUrl);
  http.setTimeout(5000); 

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    // รองรับ ArduinoJson v7
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      int geminiWeekly = doc["geminiWeekly"] | 100;
      int gemini5Hr    = doc["gemini5Hr"]    | 100;
      int claudeWeekly = doc["claudeWeekly"] | 100;
      int claude5Hr    = doc["claude5Hr"]    | 100;

      // ดึง Subtext (รองรับหลายรูปแบบกรณี API ส่งมาต่างกัน)
      String gwSub = "";
      if (doc.containsKey("geminiWeeklySubtext") && !doc["geminiWeeklySubtext"].isNull()) gwSub = doc["geminiWeeklySubtext"].as<String>();
      else if (doc.containsKey("geminiWeeklyReset")) gwSub = doc["geminiWeeklyReset"].as<String>();
      else if (doc.containsKey("geminiWeeklyDays")) gwSub = doc["geminiWeeklyDays"].as<String>() + "d";

      String g5Sub = "";
      if (doc.containsKey("gemini5HrSubtext") && !doc["gemini5HrSubtext"].isNull()) g5Sub = doc["gemini5HrSubtext"].as<String>();
      else if (doc.containsKey("gemini5HrReset")) g5Sub = doc["gemini5HrReset"].as<String>();

      String cwSub = "";
      if (doc.containsKey("claudeWeeklySubtext") && !doc["claudeWeeklySubtext"].isNull()) cwSub = doc["claudeWeeklySubtext"].as<String>();
      else if (doc.containsKey("claudeWeeklyReset")) cwSub = doc["claudeWeeklyReset"].as<String>();
      else if (doc.containsKey("claudeWeeklyDays")) cwSub = doc["claudeWeeklyDays"].as<String>() + "d";

      String c5Sub = "";
      if (doc.containsKey("claude5HrSubtext") && !doc["claude5HrSubtext"].isNull()) c5Sub = doc["claude5HrSubtext"].as<String>();
      else if (doc.containsKey("claude5HrReset")) c5Sub = doc["claude5HrReset"].as<String>();

      // ย่อข้อความให้สั้นกระชับพอดีกับจอ 2.9" (เช่น "Resets in 5 days" -> "5d left")
      auto formatSubtext = [](String s) -> String {
        s.trim();
        if (s.equalsIgnoreCase("null")) return "";
        s.replace("Resets in ", "");
        s.replace(" days", "d");
        s.replace(" day", "d");
        s.replace(" hours", "h");
        s.replace(" hour", "h");
        return s;
      };

      gwSub = formatSubtext(gwSub);
      g5Sub = formatSubtext(g5Sub);
      cwSub = formatSubtext(cwSub);
      c5Sub = formatSubtext(c5Sub);

      // ตรวจสอบว่ามีข้อมูลใดเปลี่ยนไปจากเดิมหรือไม่
      bool hasChanged = isFirstRender ||
                        (geminiWeekly != prevGeminiWeekly) ||
                        (gemini5Hr    != prevGemini5Hr)    ||
                        (claudeWeekly != prevClaudeWeekly) ||
                        (claude5Hr    != prevClaude5Hr)    ||
                        (gwSub        != prevGwTime)       ||
                        (g5Sub        != prevG5Time)       ||
                        (cwSub        != prevCwTime)       ||
                        (c5Sub        != prevC5Time);

      if (hasChanged) {
        Serial.println("[INFO] Data changed -> Updating display...");
        Serial.println("  gwSub=[" + gwSub + "] g5Sub=[" + g5Sub + "] cwSub=[" + cwSub + "] c5Sub=[" + c5Sub + "]");
        updateEinkDisplay(geminiWeekly, gemini5Hr, claudeWeekly, claude5Hr, gwSub, g5Sub, cwSub, c5Sub);
      } else {
        Serial.println("[INFO] Data unchanged -> Skipping display update.");
      }
    } else {
      Serial.println("[ERROR] JSON Parse error");
    }
  } else {
    Serial.printf("[ERROR] HTTP GET failed: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

// =========================================================================
// ฟังก์ชันจัดการแสดงผลหน้าจอ E-ink
// =========================================================================
void updateEinkDisplay(int gw, int g5, int cw, int c5,
                       String gwSub, String g5Sub, String cwSub, String c5Sub) {
  if (isFirstRender) {
    // วาดเทมเพลตและข้อมูลทั้งหมดในรอบแรก (Full Window Refresh)
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE); // พื้นหลังสีขาว
      
      // --- Header ---
      display.setFont(&FreeSansBold9pt7b);
      display.setTextColor(GxEPD_BLACK);
      display.setCursor(5, 18);
      display.print("Antigravity IDE Quota");

      // เส้นคั่น Header
      display.drawLine(5, 23, 291, 23, GxEPD_BLACK);

      // --- Gemini Section ---
      display.setFont(); // ฟอนต์มาตรฐาน
      display.setTextColor(GxEPD_BLACK);
      display.setCursor(5, 28);
      display.print("GEMINI MODELS");
      
      drawProgressBar(5, 38, 280, 11, gw, "Weekly", gwSub.c_str());
      drawProgressBar(5, 54, 280, 11, g5, "5 Hour", g5Sub.c_str());

      // เส้นคั่นกลาง
      display.drawLine(5, 70, 291, 70, GxEPD_BLACK);

      // --- Claude Section ---
      display.setFont();
      display.setTextColor(GxEPD_BLACK);
      display.setCursor(5, 75);
      display.print("CLAUDE & GPT MODELS");
      
      drawProgressBar(5, 85, 280, 11, cw, "Weekly", cwSub.c_str());
      drawProgressBar(5, 101, 280, 11, c5, "5 Hour", c5Sub.c_str());

      // ไอคอน Wi-Fi มุมขวาบน
      drawWiFiIconEink(275, 5);

    } while (display.nextPage());

    isFirstRender = false;
  } else {
    // นับจำนวนแถบที่ค่าเปลี่ยน
    int changeCount = 0;
    if (gw != prevGeminiWeekly || gwSub != prevGwTime) changeCount++;
    if (g5 != prevGemini5Hr    || g5Sub != prevG5Time) changeCount++;
    if (cw != prevClaudeWeekly || cwSub != prevCwTime) changeCount++;
    if (c5 != prevClaude5Hr    || c5Sub != prevC5Time) changeCount++;

    if (changeCount == 1) {
      // เปลี่ยนแค่ 1 ค่า -> Partial Refresh เฉพาะแถวนั้น
      if (gw != prevGeminiWeekly || gwSub != prevGwTime) updateSingleBarPartial(36, 16, gw, "Weekly", gwSub.c_str());
      if (g5 != prevGemini5Hr    || g5Sub != prevG5Time) updateSingleBarPartial(52, 16, g5, "5 Hour", g5Sub.c_str());
      if (cw != prevClaudeWeekly || cwSub != prevCwTime) updateSingleBarPartial(83, 16, cw, "Weekly", cwSub.c_str());
      if (c5 != prevClaude5Hr    || c5Sub != prevC5Time) updateSingleBarPartial(99, 16, c5, "5 Hour", c5Sub.c_str());
    } else if (changeCount > 1) {
      // เปลี่ยนหลายค่าพร้อมกัน -> Partial Refresh รวมโซนข้อมูลในรอบเดียว
      display.setPartialWindow(0, 26, display.width(), 98);
      display.firstPage();
      do {
        display.fillRect(0, 26, display.width(), 98, GxEPD_WHITE);
        
        // Gemini Section
        display.setFont();
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(5, 28);
        display.print("GEMINI MODELS");
        drawProgressBar(5, 38, 280, 11, gw, "Weekly", gwSub.c_str());
        drawProgressBar(5, 54, 280, 11, g5, "5 Hour", g5Sub.c_str());

        // เส้นคั่นกลาง
        display.drawLine(5, 70, 291, 70, GxEPD_BLACK);

        // Claude Section
        display.setFont();
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(5, 75);
        display.print("CLAUDE & GPT MODELS");
        drawProgressBar(5, 85, 280, 11, cw, "Weekly", cwSub.c_str());
        drawProgressBar(5, 101, 280, 11, c5, "5 Hour", c5Sub.c_str());
      } while (display.nextPage());
    }
  }

  // บันทึกค่าล่าสุด
  prevGeminiWeekly = gw;
  prevGemini5Hr    = g5;
  prevClaudeWeekly = cw;
  prevClaude5Hr    = c5;
  prevGwTime       = gwSub;
  prevG5Time       = g5Sub;
  prevCwTime       = cwSub;
  prevC5Time       = c5Sub;
}

// =========================================================================
// ฟังก์ชันอัปเดตเฉพาะแถวบาร์ที่ระบุ (Partial Refresh Byte-Aligned)
// =========================================================================
void updateSingleBarPartial(int y, int h, int percent, const char* label, const char* extraInfo) {
  display.setPartialWindow(0, y, display.width(), h);
  display.firstPage();
  do {
    // ล้างเฉพาะแถบความสูงนี้ด้วยสีขาว
    display.fillRect(0, y, display.width(), h, GxEPD_WHITE);
    drawProgressBar(5, y + 2, 280, 11, percent, label, extraInfo);
  } while (display.nextPage());
}

// =========================================================================
// ฟังก์ชันวาดกราฟแท่ง (Progress Bar) พร้อมแสดง % และจำนวนวันที่เหลือ (สั้นลงเพื่อเว้นที่)
// =========================================================================
void drawProgressBar(int x, int y, int w, int h, int percent, const char* label, const char* extraInfo) {
#ifdef USE_3COLOR_DISPLAY
  uint16_t color = (percent < 10) ? GxEPD_RED : GxEPD_BLACK;
#else
  uint16_t color = GxEPD_BLACK;
#endif

  display.setFont(); // ใช้ฟอนต์มาตรฐาน
  display.setTextColor(color);

  // 1. พิมพ์ Label (เช่น Weekly, 5 Hour)
  display.setCursor(x, y + 2);
  display.print(label);

  // 2. พิมพ์ตัวเลขเปอร์เซ็นต์
  display.setCursor(x + 44, y + 2);
  display.print(percent);
  display.print("%");

  // 3. วาดกรอบสี่เหลี่ยมของแถบบาร์ (ปรับความกว้างเป็น 130px เพื่อให้มีพื้นที่เหลือทางขวาสำหรับ Subtext)
  int barX = x + 68;
  int barW = 130;
  display.drawRect(barX, y, barW, h, color);

  // 4. เติมสีแถบ Progress ด้านใน
  int fillW = (percent * (barW - 2)) / 100;
  if (fillW > 0) {
    display.fillRect(barX + 1, y + 1, fillW, h - 2, color);
  }

  // 5. แสดงข้อความจำนวนวันที่เหลือ / เวลาที่เหลือ (ทางขวาของแถบบาร์)
  if (extraInfo != nullptr && strlen(extraInfo) > 0) {
    display.setCursor(barX + barW + 8, y + 2);
    display.print(extraInfo);
  }
}

// =========================================================================
// ฟังก์ชันวาดหน้าจอตอน Boot
// =========================================================================
void drawBootScreen(const char* text) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(10, 60);
    display.print(text);
  } while (display.nextPage());
}

// =========================================================================
// ฟังก์ชันวาดไอคอน Wi-Fi สำหรับจอ E-ink
// =========================================================================
void drawWiFiIconEink(int x, int y) {
  int cx = x + 8;
  int cy = y + 12;
  
  display.drawCircle(cx, cy, 8, GxEPD_BLACK);
  display.drawCircle(cx, cy, 5, GxEPD_BLACK);
  display.fillCircle(cx, cy - 1, 2, GxEPD_BLACK);
  
  // ตัดครึ่งล่างทิ้งให้เป็นรูปพัดสัญญาณ
  display.fillRect(x, cy - 2, 16, 12, GxEPD_WHITE);
  display.fillTriangle(cx, cy, x, cy, x, cy - 9, GxEPD_WHITE);
  display.fillTriangle(cx, cy, x + 16, cy, x + 16, cy - 9, GxEPD_WHITE);
}