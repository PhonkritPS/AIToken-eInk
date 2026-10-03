#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>

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

#define EPD_CS D8   // GPIO15
#define EPD_DC D3   // GPIO0
#define EPD_RST D4  // GPIO2
#define EPD_BUSY D2 // GPIO4

// ขาสำหรับจอที่ 2 (แชร์ DC, RST, SCL, SDI ร่วมกับจอแรก)
#define EPD2_CS D1   // GPIO5
#define EPD2_BUSY D6 // GPIO12

// =========================================================================
// เลือกรุ่นหน้าจอ E-ink (ปิดโหมด 3 สี เพื่อใช้จอ ขาว-ดำ รุ่นใหม่)
// =========================================================================
// #define USE_3COLOR_DISPLAY  // <--- คอมเมนต์บรรทัดนี้ไว้เพื่อใช้จอ ขาว-ดำ

#ifndef GxEPD_RED
#define GxEPD_RED 0xF800 // แม้เป็นจอขาว-ดำ ก็ประกาศเผื่อไว้กัน Error
#endif

#ifdef USE_3COLOR_DISPLAY
#include <GxEPD2_3C.h>
// จอ 2.9" 3 สี (Black / White / Red) รหัส ZJYE290S08ROG01 (GDEM029C90)
GxEPD2_3C<GxEPD2_290_C90c, GxEPD2_290_C90c::HEIGHT>
    display(GxEPD2_290_C90c(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
#else
#include <GxEPD2_BW.h>
// จอ 2.9" ขาว-ดำ สำหรับสายแพร E029A01 (ชิป SSD1608 / GDEH029A1)
GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT>
    display(GxEPD2_290(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));
#endif

// จอที่ 2 (ขาว-ดำ 2.9")
GxEPD2_BW<GxEPD2_290, GxEPD2_290::HEIGHT>
    display2(GxEPD2_290(EPD2_CS, EPD_DC, EPD_RST, EPD2_BUSY));

// =========================================================================
// การตั้งค่า Wi-Fi และ API
// =========================================================================
const char *ssid = "IT-TEST";
const char *password = "ilovephonkrit";

// API Config
const char *apiUrl = "http://172.100.2.122:5000/api/quota";
const unsigned long refreshInterval = 20000; // เช็ค API ทุก 20 วินาที
unsigned long lastFetchTime = 0;

const int fullRefreshEvery = 30; // Full Refresh ทุกๆ 30 ครั้งเพื่อล้าง ghosting

// =========================================================================
// โครงสร้างข้อมูลแต่ละบริการ และสถานะก่อนหน้าเพื่อเช็คการเปลี่ยนแปลง
// =========================================================================
// จอ 2: Antigravity IDE
struct AntigravityData {
  bool ideRunning = true;
  int geminiWeekly = 100;
  int gemini5Hr = 100;
  int claudeWeekly = 100;
  int claude5Hr = 100;
  String geminiWeeklyReset = "";
  String gemini5HrReset = "";
  String claudeWeeklyReset = "";
  String claude5HrReset = "";
  String lastUpdated = "";

  bool operator!=(const AntigravityData &o) const {
    return ideRunning != o.ideRunning ||
           geminiWeekly != o.geminiWeekly || gemini5Hr != o.gemini5Hr ||
           claudeWeekly != o.claudeWeekly || claude5Hr != o.claude5Hr ||
           geminiWeeklyReset != o.geminiWeeklyReset ||
           gemini5HrReset != o.gemini5HrReset ||
           claudeWeeklyReset != o.claudeWeeklyReset ||
           claude5HrReset != o.claude5HrReset;
  }
};

struct ContainerInfo {
  String name = "";
  String cpuStr = "0.00%";
  int cpuVal = 0;
  bool running = true;
};

// จอ 1: Spark Local AI (Ollama & Hardware Telemetry)
struct SparkData {
  bool connected = false;
  String model = "Local AI";
  String status = "Offline";
  int cpu = 0;
  int ram = 0;
  String ramRatio = "0G/0G";
  String totalTokens = "0";
  String todayTokens = "0";
  String speed = "";
  String lastUpdated = "";

  // Container stats (Docker stats)
  int containerCount = 0;
  ContainerInfo containers[3];

  bool operator!=(const SparkData &o) const {
    if (connected != o.connected || model != o.model ||
        status != o.status || cpu != o.cpu || ram != o.ram ||
        ramRatio != o.ramRatio || totalTokens != o.totalTokens ||
        todayTokens != o.todayTokens || speed != o.speed ||
        containerCount != o.containerCount) {
      return true;
    }
    for (int i = 0; i < 3; i++) {
      if (containers[i].name != o.containers[i].name ||
          containers[i].cpuStr != o.containers[i].cpuStr ||
          containers[i].cpuVal != o.containers[i].cpuVal ||
          containers[i].running != o.containers[i].running) {
        return true;
      }
    }
    return false;
  }
};

// จอ 2: Claude Code CLI
struct ClaudeCodeData {
  String planType = "";
  int weekly = 100;
  int fiveHr = 100;
  String weeklySub = "";
  String fiveHrSub = "";
  String todayTotal = "0";
  String todayCache = "0";
  String windowTotal = "0";
  String windowCache = "0";
  bool rateLimited = false;
  String rateLimitReset = "";
  String lastUpdated = "";

  bool operator!=(const ClaudeCodeData &o) const {
    return planType != o.planType || weekly != o.weekly || fiveHr != o.fiveHr ||
           weeklySub != o.weeklySub || fiveHrSub != o.fiveHrSub ||
           todayTotal != o.todayTotal || todayCache != o.todayCache ||
           windowTotal != o.windowTotal || windowCache != o.windowCache ||
           rateLimited != o.rateLimited || rateLimitReset != o.rateLimitReset;
  }
};

// จอ 1: OpenAI Codex CLI
struct CodexData {
  bool connected = false;
  String planType = "";
  int primaryPercent = -1;
  String primaryReset = "";
  int secondaryPercent = -1;
  String secondaryReset = "";
  bool rateLimited = false;
  String rateLimitReset = "";
  String lastUpdated = "";

  bool operator!=(const CodexData &o) const {
    return connected != o.connected || planType != o.planType ||
           primaryPercent != o.primaryPercent || primaryReset != o.primaryReset ||
           rateLimited != o.rateLimited || rateLimitReset != o.rateLimitReset;
  }
};

AntigravityData prevAg;
SparkData prevSpark;
ClaudeCodeData prevCc;
CodexData prevCodex;

bool isFirstRender1 = true;
bool isFirstRender2 = true;
int partialCount1 = 0;
int partialCount2 = 0;

// Prototype functions
void fetchAndDisplayQuota();
void updateDisplay1(const CodexData &codex, const SparkData &spark);
void drawDisplay1Data(const CodexData &codex, const SparkData &spark);
void updateDisplay2(const ClaudeCodeData &cc, const AntigravityData &ag);
void drawDisplay2Data(const ClaudeCodeData &cc, const AntigravityData &ag);
void drawMiniBar(Adafruit_GFX &gfx, int x, int y, int w, int h, int percent);
void printBold(Adafruit_GFX &gfx, int x, int y, const String &text);
void drawRateLimitBanner(Adafruit_GFX &gfx, int x, int y, const String &resetText);
void drawWiFiIconEink(Adafruit_GFX &gfx, int x, int y);
void drawBootScreen(const char *text);
void drawBootScreen2(const char *text);
String formatPlanName(String plan);

// =========================================================================
// Setup
// =========================================================================
void setup() {
  Serial.begin(115200);

  // ตั้งค่าขา CS ของทั้งสองจอให้เป็น HIGH ก่อน ป้องกันสัญญาณ SPI ชนกัน
  pinMode(EPD_CS, OUTPUT);
  digitalWrite(EPD_CS, HIGH);
  pinMode(EPD2_CS, OUTPUT);
  digitalWrite(EPD2_CS, HIGH);

  // 1. เริ่มการทำงานจอที่ 1 (Codex + SPARK Local)
  display.init(115200);
  display.setRotation(1); // แนวนอน 296 x 128
  display.setTextColor(GxEPD_BLACK);

  // 2. เริ่มการทำงานจอที่ 2 (Claude Code + Antigravity)
  display2.init(115200);
  display2.setRotation(1); // แนวนอน 296 x 128
  display2.setTextColor(GxEPD_BLACK);

  drawBootScreen2("Claude Code\n& Antigravity");
  drawBootScreen("AIToken Monitor\nConnecting to Wi-Fi...");

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
    fetchAndDisplayQuota(); // โหลดและวาดข้อมูลจริงทันที ไม่ต้องวาดหน้าคั่นใหม่อีกรอบ
  } else {
    drawBootScreen("WiFi Failed.\nPlease Check Network");
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

    // รองรับ ArduinoJson v7 / v6
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      auto formatSubtext = [](String s) -> String {
        s.trim();
        if (s.equalsIgnoreCase("null")) return "";
        s.replace("Resets in ", "");
        s.replace(" days", "d");
        s.replace(" day", "d");
        s.replace(" hours", "h");
        s.replace(" hour", "h");
        s.replace(" minutes", "m");
        s.replace(" minute", "m");
        return s;
      };

      // -------------------------------------------------------------
      // 1. Antigravity IDE (Gemini & Claude/GPT)
      // -------------------------------------------------------------
      AntigravityData ag;
      ag.ideRunning = doc.containsKey("ideRunning") ? doc["ideRunning"].as<bool>() : true;
      ag.geminiWeekly = doc["geminiWeekly"] | 100;
      ag.gemini5Hr = doc["gemini5Hr"] | 100;
      ag.claudeWeekly = doc["claudeWeekly"] | 100;
      ag.claude5Hr = doc["claude5Hr"] | 100;

      String gwSub = doc["geminiWeeklyReset"] | doc["geminiWeeklySubtext"] | "";
      String g5Sub = doc["gemini5HrReset"] | doc["gemini5HrSubtext"] | "";
      String cwSub = doc["claudeWeeklyReset"] | doc["claudeWeeklySubtext"] | "";
      String c5Sub = doc["claude5HrReset"] | doc["claude5HrSubtext"] | "";

      ag.geminiWeeklyReset = formatSubtext(gwSub);
      ag.gemini5HrReset = formatSubtext(g5Sub);
      ag.claudeWeeklyReset = formatSubtext(cwSub);
      ag.claude5HrReset = formatSubtext(c5Sub);
      ag.lastUpdated = doc["lastUpdated"] | "";

      // -------------------------------------------------------------
      // 2. SPARK Local AI
      // -------------------------------------------------------------
      SparkData spark;
      spark.connected = doc["sparkConnected"] | false;
      spark.model = doc["sparkModel"] | "Local AI";
      spark.status = doc["sparkStatus"] | "Offline";
      spark.cpu = doc["sparkCpu"] | 0;
      spark.ram = doc["sparkRam"] | 0;
      spark.ramRatio = doc["sparkRamRatio"] | "0G/0G";
      spark.totalTokens = doc["sparkTotalTokens"] | "0";
      spark.todayTokens = doc["sparkTodayTokens"] | "0";
      spark.speed = doc["sparkSpeed"] | "";
      spark.lastUpdated = doc["sparkLastUpdated"] | "";

      auto parseCpuInfo = [](JsonVariant v, ContainerInfo &ci) {
        if (v.isNull()) {
          ci.cpuStr = "0.00%";
          ci.cpuVal = 0;
          return;
        }
        if (v.is<const char*>() || v.is<String>()) {
          String s = v.as<String>();
          s.trim();
          if (s.length() > 0 && !s.endsWith("%")) s += "%";
          ci.cpuStr = s;
          String numOnly = s;
          numOnly.replace("%", "");
          ci.cpuVal = constrain((int)round(numOnly.toFloat()), 0, 100);
        } else if (v.is<float>()) {
          float f = v.as<float>();
          char buf[16];
          snprintf(buf, sizeof(buf), "%.2f%%", f);
          ci.cpuStr = String(buf);
          ci.cpuVal = constrain((int)round(f), 0, 100);
        } else if (v.is<int>()) {
          int i = v.as<int>();
          ci.cpuStr = String(i) + ".00%";
          ci.cpuVal = constrain(i, 0, 100);
        }
      };

      // ดึงข้อมูล Docker containers (รองรับทั้ง Array และคีย์แยก)
      spark.containerCount = 0;
      if (doc["sparkContainers"].is<JsonArray>()) {
        JsonArray arr = doc["sparkContainers"].as<JsonArray>();
        for (JsonObject c : arr) {
          if (spark.containerCount >= 3) break;
          String cName = c["name"] | c["container"] | "";
          if (cName.length() > 0) {
            ContainerInfo ci;
            ci.name = cName;
            parseCpuInfo(c["cpu"], ci);
            ci.running = c.containsKey("running") ? c["running"].as<bool>() : true;
            if (!ci.running) {
              ci.cpuStr = "OFF";
              ci.cpuVal = 0;
            }
            spark.containers[spark.containerCount++] = ci;
          }
        }
      }

      if (spark.containerCount == 0) {
        if (doc.containsKey("sparkC1Name") || doc.containsKey("sparkC1Cpu") || doc.containsKey("comfyuiCpu")) {
          ContainerInfo ci1;
          ci1.name = doc["sparkC1Name"] | "comfyui-spark";
          parseCpuInfo(doc.containsKey("sparkC1Cpu") ? doc["sparkC1Cpu"] : doc["comfyuiCpu"], ci1);
          ci1.running = doc.containsKey("sparkC1Running") ? doc["sparkC1Running"].as<bool>() : true;
          if (!ci1.running) { ci1.cpuStr = "OFF"; ci1.cpuVal = 0; }
          spark.containers[spark.containerCount++] = ci1;
        }
        if (doc.containsKey("sparkC2Name") || doc.containsKey("sparkC2Cpu") || doc.containsKey("ollamaCpu")) {
          ContainerInfo ci2;
          ci2.name = doc["sparkC2Name"] | ("ollama-" + spark.model);
          parseCpuInfo(doc.containsKey("sparkC2Cpu") ? doc["sparkC2Cpu"] : doc["ollamaCpu"], ci2);
          ci2.running = doc.containsKey("sparkC2Running") ? doc["sparkC2Running"].as<bool>() : true;
          if (!ci2.running) { ci2.cpuStr = "OFF"; ci2.cpuVal = 0; }
          spark.containers[spark.containerCount++] = ci2;
        }
      }


      // -------------------------------------------------------------
      // 3. Claude Code CLI
      // -------------------------------------------------------------
      ClaudeCodeData cc;
      cc.planType = doc["ccPlanType"] | "";
      cc.weekly = doc["ccWeekly"] | 100;
      cc.fiveHr = doc["cc5Hr"] | 100;
      cc.weeklySub = formatSubtext(doc["ccWeeklyReset"] | doc["ccWeeklySubtext"] | "");
      cc.fiveHrSub = formatSubtext(doc["cc5HrReset"] | doc["cc5HrSubtext"] | "");
      cc.todayTotal = doc["ccTodayTokens"] | "0";
      cc.todayCache = doc["ccTodayCache"] | "0";
      cc.windowTotal = doc["ccWindowTokens"] | "0";
      cc.windowCache = doc["ccWindowCache"] | "0";
      cc.rateLimited = doc["ccRateLimited"] | false;
      cc.rateLimitReset = doc["ccRateLimitReset"] | "";
      cc.lastUpdated = doc["ccLastUpdated"] | "";

      // -------------------------------------------------------------
      // 4. OpenAI Codex CLI
      // -------------------------------------------------------------
      CodexData codex;
      codex.connected = doc["codexConnected"] | false;
      codex.planType = doc["codexPlanType"] | "";
      codex.primaryPercent = doc["codexPrimaryPercent"] | -1;
      codex.primaryReset = formatSubtext(doc["codexPrimaryReset"] | "");
      codex.secondaryPercent = doc["codexSecondaryPercent"] | -1;
      codex.secondaryReset = formatSubtext(doc["codexSecondaryReset"] | "");
      codex.rateLimited = doc["codexRateLimited"] | false;
      codex.rateLimitReset = doc["codexRateLimitReset"] | "";
      codex.lastUpdated = doc["codexLastUpdated"] | "";

      // -------------------------------------------------------------
      // ตรวจสอบการอัปเดตของแต่ละหน้าจอ
      // -------------------------------------------------------------
      bool disp1Changed = isFirstRender1 || (codex != prevCodex) || (spark != prevSpark);
      if (disp1Changed) {
        Serial.println("[INFO] Display 1 (Codex & SPARK) data changed -> updating...");
        updateDisplay1(codex, spark);
      } else {
        Serial.println("[INFO] Display 1 data unchanged.");
      }

      bool disp2Changed = isFirstRender2 || (cc != prevCc) || (ag != prevAg);
      if (disp2Changed) {
        Serial.println("[INFO] Display 2 (Claude Code & Antigravity) data changed -> updating...");
        updateDisplay2(cc, ag);
      } else {
        Serial.println("[INFO] Display 2 data unchanged.");
      }

    } else {
      Serial.print("[ERROR] JSON Parse error: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.printf("[ERROR] HTTP GET failed: %s (code: %d)\n",
                  http.errorToString(httpCode).c_str(), httpCode);
  }
  http.end();
}

// =========================================================================
// จอที่ 1: OPENAI CODEX & SPARK LOCAL (การ์ดกรอบมุมมน 2 ช่อง)
// =========================================================================
void updateDisplay1(const CodexData &codex, const SparkData &spark) {
  bool doFull = isFirstRender1 || partialCount1 >= fullRefreshEvery;

  if (doFull) {
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      drawDisplay1Data(codex, spark);
    } while (display.nextPage());
    isFirstRender1 = false;
    partialCount1 = 0;
  } else {
    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
      display.fillRect(0, 0, display.width(), display.height(), GxEPD_WHITE);
      drawDisplay1Data(codex, spark);
    } while (display.nextPage());
    partialCount1++;
  }
  display.powerOff();

  prevCodex = codex;
  prevSpark = spark;
}

void drawDisplay1Data(const CodexData &codex, const SparkData &spark) {
  display.setFont();
  display.setTextColor(GxEPD_BLACK);

  // -----------------------------------------------------------------------
  // CARD 1: OPENAI CODEX (กรอบบน x=2, y=2, w=292, h=38, r=4)
  // -----------------------------------------------------------------------
  display.drawRoundRect(2, 2, 292, 38, 4, GxEPD_BLACK);
  display.drawLine(2, 17, 293, 17, GxEPD_BLACK); // เส้นคั่น Header ในการ์ด

  // Header (y = 6) - เวลา last update อยู่หลังชื่อโมเดลทันที
  printBold(display, 8, 6, "OPENAI CODEX");
  if (codex.lastUpdated.length() > 0) {
    display.setCursor(88, 6);
    display.print(codex.lastUpdated);
  }

  String codexPlan = formatPlanName(codex.planType);
  if (codexPlan.length() > 0) {
    printBold(display, 142, 6, "[" + codexPlan + "]");
  }

  if (codex.rateLimited) {
    drawRateLimitBanner(display, 190, 4, codex.rateLimitReset);
  }
  drawWiFiIconEink(display, 272, 3);

  // Row 1: Weekly Limit (y = 24)
  printBold(display, 8, 24, "Weekly");
  bool hasPrimaryQuota = codex.primaryPercent >= 0 &&
                         !(codex.primaryPercent == 100 && codex.primaryReset.length() == 0);
  if (codex.connected && hasPrimaryQuota) {
    drawMiniBar(display, 52, 23, 60, 10, codex.primaryPercent);
    printBold(display, 118, 24, String(codex.primaryPercent) + "%");
    display.setCursor(154, 24);
    display.print(codex.primaryReset);
  } else if (codex.connected) {
    display.setCursor(52, 24);
    display.print("Syncing usage...");
  } else {
    printBold(display, 52, 24, "Not Connected");
  }

  // -----------------------------------------------------------------------
  // CARD 2: SPARK LOCAL (กรอบล่าง x=2, y=44, w=292, h=82, r=4)
  // -----------------------------------------------------------------------
  display.drawRoundRect(2, 44, 292, 82, 4, GxEPD_BLACK);
  display.drawLine(2, 59, 293, 59, GxEPD_BLACK); // เส้นคั่น Header ในการ์ด

  // Header (y = 48) - วินาทีกลับมา พร้อมปรับสถานะเป็น -R
  printBold(display, 6, 48, "SPARK LOCAL");
  if (spark.lastUpdated.length() > 0) {
    display.setCursor(77, 48);
    display.print(spark.lastUpdated); // แสดงเวลาเต็มพร้อมวินาที (HH:mm:ss)
  }

  // สถานะปรับเป็น -R, -A, -X ตามที่ต้องการ
  String statChar = "-R";
  if (spark.status == "Active") statChar = "-A";
  else if (!spark.connected) statChar = "-X";
  printBold(display, 129, 48, statChar);

  // Token รวมต่อท้ายสถานะ (มีระยะห่างชัดเจน)
  if (spark.connected && spark.totalTokens.length() > 0 && spark.totalTokens != "0") {
    printBold(display, 147, 48, "All: " + spark.totalTokens);
  }

  // IP ขยับเข้ามาที่ x = 214 (สิ้นสุดที่ 280) เว้นขอบขวา 13px ไม่ติดกรอบ
  display.setCursor(214, 48);
  display.print("10.104.1.23");

  if (spark.connected) {
    // Row 1: Host Telemetry - RAM & CPU แบบตัวเลขพร้อมอัตราส่วน (y = 65)
    printBold(display, 8, 65, "Host:");
    printBold(display, 44, 65, "RAM:");
    printBold(display, 72, 65, String(spark.ram) + "%");
    display.setCursor(102, 65);
    display.print(spark.ramRatio);

    printBold(display, 175, 65, "CPU:");
    printBold(display, 205, 65, String(spark.cpu) + "%");

    // Row 2 & Row 3: Containers / Models (y = 80, y = 95)
    auto formatContainerName = [](const String &rawName) -> String {
      if (rawName.startsWith("ollama-")) {
        return rawName.substring(7); // ตัด ollama- ออก แสดงชื่อโมเดลเพียวๆ ไม่มี Model:
      }
      return rawName;
    };

    if (spark.containerCount > 0) {
      // Container 1 (y = 80)
      String c1Name = formatContainerName(spark.containers[0].name);
      if (c1Name.length() > 28) c1Name = c1Name.substring(0, 26) + "..";
      display.setCursor(8, 80);
      display.print(c1Name);

      if (spark.containers[0].running) {
        drawMiniBar(display, 196, 79, 34, 9, spark.containers[0].cpuVal);
      }
      int x1 = 286 - (int)spark.containers[0].cpuStr.length() * 6;
      printBold(display, x1, 80, spark.containers[0].cpuStr);
    }

    if (spark.containerCount > 1) {
      // Container 2 / Model (y = 95)
      String c2Name = formatContainerName(spark.containers[1].name);
      if (c2Name.length() > 28) c2Name = c2Name.substring(0, 26) + "..";
      display.setCursor(8, 95);
      display.print(c2Name);

      if (spark.containers[1].running) {
        drawMiniBar(display, 196, 94, 34, 9, spark.containers[1].cpuVal);
      }
      int x2 = 286 - (int)spark.containers[1].cpuStr.length() * 6;
      printBold(display, x2, 95, spark.containers[1].cpuStr);
    }

    if (spark.containerCount == 0) {
      display.setCursor(8, 80);
      display.print("Waiting for container stats...");
    }

    // Row 4: Token Usage ย้ายลงมาอยู่ล่างสุด (y = 111)
    printBold(display, 8, 111, "Tokens:");
    String tokStr = (spark.todayTokens.length() > 0) ? spark.todayTokens : "0";
    if (spark.speed.length() > 0) {
      String sp = spark.speed;
      sp.trim();
      if (!sp.endsWith("t/s")) sp += " t/s";
      tokStr += " (" + sp + ")";
    } else {
      tokStr += " (0 t/s)";
    }
    printBold(display, 58, 111, tokStr);

  } else {
    printBold(display, 8, 80, "Status: Offline / Host Unreachable");
    display.setCursor(8, 98);
    display.print("Check Ollama or Local AI Server");
  }
}

// =========================================================================
// จอที่ 2: CLAUDE CODE & ANTIGRAVITY (การ์ดกรอบมุมมน 2 ช่อง)
// =========================================================================
void updateDisplay2(const ClaudeCodeData &cc, const AntigravityData &ag) {
  bool doFull = isFirstRender2 || partialCount2 >= fullRefreshEvery;

  if (doFull) {
    display2.setFullWindow();
    display2.firstPage();
    do {
      display2.fillScreen(GxEPD_WHITE);
      drawDisplay2Data(cc, ag);
    } while (display2.nextPage());
    isFirstRender2 = false;
    partialCount2 = 0;
  } else {
    display2.setPartialWindow(0, 0, display2.width(), display2.height());
    display2.firstPage();
    do {
      display2.fillRect(0, 0, display2.width(), display2.height(), GxEPD_WHITE);
      drawDisplay2Data(cc, ag);
    } while (display2.nextPage());
    partialCount2++;
  }
  display2.powerOff();

  prevCc = cc;
  prevAg = ag;
}

void drawDisplay2Data(const ClaudeCodeData &cc, const AntigravityData &ag) {
  display2.setFont();
  display2.setTextColor(GxEPD_BLACK);

  // -----------------------------------------------------------------------
  // CARD 1: CLAUDE CODE (กรอบบน x=2, y=2, w=292, h=60, r=4)
  // -----------------------------------------------------------------------
  display2.drawRoundRect(2, 2, 292, 60, 4, GxEPD_BLACK);
  display2.drawLine(2, 17, 293, 17, GxEPD_BLACK); // เส้นคั่น Header ในการ์ด

  // Header (y = 6) - เวลา last update อยู่หลังชื่อโมเดลทันที
  printBold(display2, 8, 6, "CLAUDE CODE");
  if (cc.lastUpdated.length() > 0) {
    display2.setCursor(84, 6);
    display2.print(cc.lastUpdated);
  }

  String ccPlan = formatPlanName(cc.planType);
  if (ccPlan.length() > 0) {
    printBold(display2, 142, 6, "[" + ccPlan + "]");
  }

  if (cc.rateLimited) {
    drawRateLimitBanner(display2, 190, 4, cc.rateLimitReset);
  }
  drawWiFiIconEink(display2, 272, 3);

  // Row 1: Plan Limits (y = 24)
  printBold(display2, 8, 24, "Limit");
  drawMiniBar(display2, 48, 23, 40, 10, cc.weekly);
  printBold(display2, 92, 24, String(cc.weekly) + "%");
  display2.setCursor(120, 24);
  display2.print(cc.weeklySub);

  printBold(display2, 160, 24, "5H");
  drawMiniBar(display2, 176, 23, 40, 10, cc.fiveHr);
  printBold(display2, 220, 24, String(cc.fiveHr) + "%");
  display2.setCursor(248, 24);
  display2.print(cc.fiveHrSub);

  // Row 2: Tokens (y = 43)
  printBold(display2, 8, 43, "Tokens");
  printBold(display2, 48, 43, "Day:");
  printBold(display2, 74, 43, cc.todayTotal);
  display2.setCursor(110, 43);
  display2.print("(c:" + cc.todayCache + ")");

  printBold(display2, 170, 43, "5H:");
  printBold(display2, 192, 43, cc.windowTotal);
  display2.setCursor(228, 43);
  display2.print("(c:" + cc.windowCache + ")");

  // -----------------------------------------------------------------------
  // CARD 2: ANTIGRAVITY (กรอบล่าง x=2, y=66, w=292, h=60, r=4)
  // -----------------------------------------------------------------------
  display2.drawRoundRect(2, 66, 292, 60, 4, GxEPD_BLACK);
  display2.drawLine(2, 81, 293, 81, GxEPD_BLACK); // เส้นคั่น Header ในการ์ด

  // Header (y = 70)
  printBold(display2, 8, 70, "ANTIGRAVITY");

  if (ag.ideRunning) {
    if (ag.lastUpdated.length() > 0) {
      display2.setCursor(84, 70);
      display2.print(ag.lastUpdated);
    }

    // Row 1: Gemini (y = 88)
    printBold(display2, 8, 88, "Gemini");
    drawMiniBar(display2, 50, 87, 40, 10, ag.geminiWeekly);
    printBold(display2, 94, 88, String(ag.geminiWeekly) + "%");
    display2.setCursor(122, 88);
    display2.print(ag.geminiWeeklyReset);

    printBold(display2, 160, 88, "5H");
    drawMiniBar(display2, 176, 87, 40, 10, ag.gemini5Hr);
    printBold(display2, 220, 88, String(ag.gemini5Hr) + "%");
    display2.setCursor(248, 88);
    display2.print(ag.gemini5HrReset);

    // Row 2: Claude & GPT (y = 107)
    printBold(display2, 8, 107, "Claude");
    drawMiniBar(display2, 50, 106, 40, 10, ag.claudeWeekly);
    printBold(display2, 94, 107, String(ag.claudeWeekly) + "%");
    display2.setCursor(122, 107);
    display2.print(ag.claudeWeeklyReset);

    printBold(display2, 160, 107, "5H");
    drawMiniBar(display2, 176, 106, 40, 10, ag.claude5Hr);
    printBold(display2, 220, 107, String(ag.claude5Hr) + "%");
    display2.setCursor(248, 107);
    display2.print(ag.claude5HrReset);
  } else {
    printBold(display2, 84, 70, "[CLOSED]");
    printBold(display2, 8, 92, "IDE Not Running / Closed");
    display2.setCursor(8, 107);
    display2.print("Open Antigravity IDE to view live quota");
  }
}

String formatPlanName(String plan) {
  plan.trim();
  plan.toLowerCase();

  // The Codex auth API reports ChatGPT Pro as "prolite".
  if (plan == "prolite" || plan == "pro_lite" || plan == "pro-lite") return "PRO";
  if (plan == "chatgptplus" || plan == "chatgpt_plus") return "PLUS";
  if (plan == "chatgptteam" || plan == "chatgpt_team") return "TEAM";

  plan.toUpperCase();
  return plan;
}

// =========================================================================
// ฟังก์ชันวาดแถบ Progress Bar ขนาดกะทัดรัด (Mini Bar)
// =========================================================================
void drawMiniBar(Adafruit_GFX &gfx, int x, int y, int w, int h, int percent) {
  percent = constrain(percent, 0, 100);
  gfx.drawRect(x, y, w, h, GxEPD_BLACK);
  int fillW = (percent * (w - 2)) / 100;
  if (fillW > 0) {
    gfx.fillRect(x + 1, y + 1, fillW, h - 2, GxEPD_BLACK);
  }
}

// =========================================================================
// พิมพ์ข้อความแบบตัวหนาเทียม (วาดซ้ำเลื่อน 1px ไปทางขวา)
// =========================================================================
void printBold(Adafruit_GFX &gfx, int x, int y, const String &text) {
  gfx.setCursor(x, y);
  gfx.print(text);
  gfx.setCursor(x + 1, y);
  gfx.print(text);
}

// =========================================================================
// แถบเตือนโดน 429 Rate Limit (พื้นดำ-ตัวหนังสือขาว)
// =========================================================================
void drawRateLimitBanner(Adafruit_GFX &gfx, int x, int y, const String &resetText) {
  String msg = "RATE LIMIT";
  if (resetText.length() > 0) msg += " " + resetText;
  int boxW = (int)msg.length() * 6 + 6;
  int boxH = 11;

  gfx.fillRect(x, y, boxW, boxH, GxEPD_BLACK);
  gfx.setFont();
  gfx.setTextColor(GxEPD_WHITE);
  gfx.setCursor(x + 3, y + 2);
  gfx.print(msg);
  gfx.setTextColor(GxEPD_BLACK);
}

// =========================================================================
// ฟังก์ชันวาดไอคอน Wi-Fi สำหรับจอ E-ink
// =========================================================================
void drawWiFiIconEink(Adafruit_GFX &gfx, int x, int y) {
  int cx = x + 8;
  int cy = y + 12;

  gfx.drawCircle(cx, cy, 8, GxEPD_BLACK);
  gfx.drawCircle(cx, cy, 5, GxEPD_BLACK);
  gfx.fillCircle(cx, cy - 1, 2, GxEPD_BLACK);

  // ตัดครึ่งล่างทิ้งให้เป็นรูปพัดสัญญาณ
  gfx.fillRect(x, cy - 2, 16, 12, GxEPD_WHITE);
  gfx.fillTriangle(cx, cy, x, cy, x, cy - 9, GxEPD_WHITE);
  gfx.fillTriangle(cx, cy, x + 16, cy, x + 16, cy - 9, GxEPD_WHITE);
}

// =========================================================================
// หน้าจอ Boot
// =========================================================================
void drawBootScreen(const char *text) {
  String t(text);
  int nl = t.indexOf('\n');
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setFont(&FreeSans9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(10, 50);
    display.print(nl < 0 ? t : t.substring(0, nl));
    if (nl >= 0) {
      display.setCursor(10, 75);
      display.print(t.substring(nl + 1));
    }
  } while (display.nextPage());
  display.powerOff();
}

void drawBootScreen2(const char *text) {
  String t(text);
  int nl = t.indexOf('\n');
  display2.setFullWindow();
  display2.firstPage();
  do {
    display2.fillScreen(GxEPD_WHITE);
    display2.setFont(&FreeSans9pt7b);
    display2.setTextColor(GxEPD_BLACK);
    display2.setCursor(10, 50);
    display2.print(nl < 0 ? t : t.substring(0, nl));
    if (nl >= 0) {
      display2.setCursor(10, 75);
      display2.print(t.substring(nl + 1));
    }
  } while (display2.nextPage());
  display2.powerOff();
}
