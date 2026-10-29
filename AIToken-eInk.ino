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
  int argonWeekly = -1; // -1 = IDE รุ่นนี้ไม่มีกลุ่ม Gemini 4 Argon แยก -> ไม่ต้องวาดแถว
  String geminiWeeklyReset = "";
  String gemini5HrReset = "";
  String claudeWeeklyReset = "";
  String claude5HrReset = "";
  String argonWeeklyReset = "";
  String lastUpdated = "";

  bool operator!=(const AntigravityData &o) const {
    return ideRunning != o.ideRunning ||
           geminiWeekly != o.geminiWeekly || gemini5Hr != o.gemini5Hr ||
           claudeWeekly != o.claudeWeekly || claude5Hr != o.claude5Hr ||
           argonWeekly != o.argonWeekly ||
           geminiWeeklyReset != o.geminiWeeklyReset ||
           gemini5HrReset != o.gemini5HrReset ||
           claudeWeeklyReset != o.claudeWeeklyReset ||
           claude5HrReset != o.claude5HrReset ||
           argonWeeklyReset != o.argonWeeklyReset;
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

// จอ 1: AI9700 Local AI (llama-server บน Radeon AI PRO R9700)
struct Ai9700Data {
  bool connected = false;
  String model = "Local AI";
  String status = "Offline";
  int cpu = 0;
  int ram = 0;
  String ramRatio = "0G/0G";
  int gpu = 0;          // GPU use %
  String vramRatio = "0G/0G";
  int slotsBusy = 0;
  int slotsTotal = 0;
  String totalTokens = "0";
  String todayTokens = "0";
  String speed = "";
  String lastUpdated = "";

  bool operator!=(const Ai9700Data &o) const {
    return connected != o.connected || model != o.model || status != o.status ||
           cpu != o.cpu || ram != o.ram || ramRatio != o.ramRatio ||
           gpu != o.gpu || vramRatio != o.vramRatio ||
           slotsBusy != o.slotsBusy || slotsTotal != o.slotsTotal ||
           totalTokens != o.totalTokens || todayTokens != o.todayTokens ||
           speed != o.speed;
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

// จอ 2: OpenAI Codex CLI
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
Ai9700Data prevAi9700;
ClaudeCodeData prevCc;
CodexData prevCodex;

bool isFirstRender1 = true;
bool isFirstRender2 = true;
int partialCount1 = 0;
int partialCount2 = 0;

// Prototype functions
void fetchAndDisplayQuota();
void updateDisplay1(const SparkData &spark, const Ai9700Data &ai);
void drawDisplay1Data(const SparkData &spark, const Ai9700Data &ai);
void drawHostRow(Adafruit_GFX &gfx, int y, int ram, const String &ramRatio, int cpu,
                 const String &todayTokens, const String &speed);
void drawUsageRow(Adafruit_GFX &gfx, int y, const String &label, const String &detail,
                  bool showBar, int barPercent, const String &rightText);
void drawLocalAiHeader(Adafruit_GFX &gfx, int y, const String &title, const String &lastUpdated,
                       bool connected, const String &status, const String &totalTokens,
                       const String &ip);
void updateDisplay2(const ClaudeCodeData &cc, const AntigravityData &ag, const CodexData &codex);
void drawDisplay2Data(const ClaudeCodeData &cc, const AntigravityData &ag, const CodexData &codex);
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

  // 1. เริ่มการทำงานจอที่ 1 (SPARK Local เต็มจอ)
  display.init(115200);
  display.setRotation(1); // แนวนอน 296 x 128
  display.setTextColor(GxEPD_BLACK);

  // 2. เริ่มการทำงานจอที่ 2 (Antigravity + Claude Code + Codex)
  display2.init(115200);
  display2.setRotation(1); // แนวนอน 296 x 128
  display2.setTextColor(GxEPD_BLACK);

  drawBootScreen2("Antigravity\n& Claude Code");
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
      ag.argonWeekly = doc["argonWeekly"] | -1;

      String gwSub = doc["geminiWeeklyReset"] | doc["geminiWeeklySubtext"] | "";
      String g5Sub = doc["gemini5HrReset"] | doc["gemini5HrSubtext"] | "";
      String cwSub = doc["claudeWeeklyReset"] | doc["claudeWeeklySubtext"] | "";
      String c5Sub = doc["claude5HrReset"] | doc["claude5HrSubtext"] | "";
      String awSub = doc["argonWeeklyReset"] | "";

      ag.geminiWeeklyReset = formatSubtext(gwSub);
      ag.gemini5HrReset = formatSubtext(g5Sub);
      ag.claudeWeeklyReset = formatSubtext(cwSub);
      ag.claude5HrReset = formatSubtext(c5Sub);
      ag.argonWeeklyReset = formatSubtext(awSub);
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
      // 2.1 AI9700 Local AI (llama-server)
      // -------------------------------------------------------------
      Ai9700Data ai;
      ai.connected = doc["ai9700Connected"] | false;
      ai.model = doc["ai9700Model"] | "Local AI";
      ai.status = doc["ai9700Status"] | "Offline";
      ai.cpu = doc["ai9700Cpu"] | 0;
      ai.ram = doc["ai9700Ram"] | 0;
      ai.ramRatio = doc["ai9700RamRatio"] | "0G/0G";
      ai.gpu = doc["ai9700Gpu"] | 0;
      ai.vramRatio = doc["ai9700VramRatio"] | "0G/0G";
      ai.slotsBusy = doc["ai9700SlotsBusy"] | 0;
      ai.slotsTotal = doc["ai9700SlotsTotal"] | 0;
      ai.totalTokens = doc["ai9700TotalTokens"] | "0";
      ai.todayTokens = doc["ai9700TodayTokens"] | "0";
      ai.speed = doc["ai9700Speed"] | "";
      ai.lastUpdated = doc["ai9700LastUpdated"] | "";

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
      bool disp1Changed = isFirstRender1 || (spark != prevSpark) || (ai != prevAi9700);
      if (disp1Changed) {
        Serial.println("[INFO] Display 1 (SPARK & AI9700) data changed -> updating...");
        updateDisplay1(spark, ai);
      } else {
        Serial.println("[INFO] Display 1 data unchanged.");
      }

      bool disp2Changed = isFirstRender2 || (cc != prevCc) || (ag != prevAg) || (codex != prevCodex);
      if (disp2Changed) {
        Serial.println("[INFO] Display 2 (Antigravity & Claude Code & Codex) data changed -> updating...");
        updateDisplay2(cc, ag, codex);
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
// จอที่ 1: SPARK LOCAL & AI9700 (การ์ดกรอบมุมมน 2 ช่อง)
// ใช้กฎช่องว่างเดียวกับจอ 2: ทุกช่องว่าง 4px, ตัวอักษร/บาร์สูง 7px วาดที่ y เดียวกัน
//   SPARK LOCAL y=0  h=66 (4 แถว: Host + container 3 ตัว)
//   AI9700      y=73 h=55 (3 แถว: Host + GPU/VRAM + โมเดล/slot) จบที่ y=127 ชิดขอบล่าง
// =========================================================================
void updateDisplay1(const SparkData &spark, const Ai9700Data &ai) {
  bool doFull = isFirstRender1 || partialCount1 >= fullRefreshEvery;

  if (doFull) {
    display.setFullWindow();
    display.firstPage();
    do {
      display.fillScreen(GxEPD_WHITE);
      drawDisplay1Data(spark, ai);
    } while (display.nextPage());
    isFirstRender1 = false;
    partialCount1 = 0;
  } else {
    display.setPartialWindow(0, 0, display.width(), display.height());
    display.firstPage();
    do {
      display.fillRect(0, 0, display.width(), display.height(), GxEPD_WHITE);
      drawDisplay1Data(spark, ai);
    } while (display.nextPage());
    partialCount1++;
  }
  display.powerOff();

  prevSpark = spark;
  prevAi9700 = ai;
}

// หัวการ์ด Local AI: ชื่อ | เวลา | สถานะ -R/-A/-X | All: token สะสม | IP
void drawLocalAiHeader(Adafruit_GFX &gfx, int y, const String &title, const String &lastUpdated,
                       bool connected, const String &status, const String &totalTokens,
                       const String &ip) {
  printBold(gfx, 6, y, title);
  if (lastUpdated.length() > 0) {
    gfx.setCursor(77, y);
    gfx.print(lastUpdated); // แสดงเวลาเต็มพร้อมวินาที (HH:mm:ss)
  }

  String statChar = "-R";
  if (status == "Active") statChar = "-A";
  else if (!connected) statChar = "-X";
  printBold(gfx, 129, y, statChar);

  if (connected && totalTokens.length() > 0 && totalTokens != "0") {
    printBold(gfx, 147, y, "All: " + totalTokens);
  }

  // IP ที่ x = 214 (สิ้นสุดที่ 280)
  gfx.setCursor(214, y);
  gfx.print(ip);
}

// แถว Host: RAM: xx% ratio  CPU: xx%  ........  T: token (speed t/s) ชิดขวา
void drawHostRow(Adafruit_GFX &gfx, int y, int ram, const String &ramRatio, int cpu,
                 const String &todayTokens, const String &speed) {
  int rx = 8;
  auto drawField = [&](const String &text, bool bold) {
    if (bold) printBold(gfx, rx, y, text);
    else {
      gfx.setCursor(rx, y);
      gfx.print(text);
    }
    rx += (int)text.length() * 6;
  };

  drawField("RAM:", true);
  rx += 2;
  drawField(String(ram) + "%", true);
  rx += 4;
  drawField(ramRatio, false);
  rx += 6;
  drawField("CPU:", true);
  rx += 2;
  drawField(String(cpu) + "%", true);

  String tokStr = (todayTokens.length() > 0) ? todayTokens : "0";
  String sp = speed;
  sp.trim();
  if (sp.length() == 0) sp = "0";
  if (!sp.endsWith("t/s")) sp += " t/s";
  String tokLine = "T: " + tokStr + " (" + sp + ")";
  printBold(gfx, 286 - (int)tokLine.length() * 6, y, tokLine);
}

// แถวใช้งาน: [label ตัวหนา] detail ....... [บาร์] ค่าชิดขวา (แบบเดียวกับแถว container)
void drawUsageRow(Adafruit_GFX &gfx, int y, const String &label, const String &detail,
                  bool showBar, int barPercent, const String &rightText) {
  int x = 8;
  if (label.length() > 0) {
    printBold(gfx, x, y, label);
    x += (int)label.length() * 6 + 6;
  }
  String d = detail;
  int maxChars = (190 - x) / 6; // ห้ามล้นไปทับบาร์ที่ x = 196
  if ((int)d.length() > maxChars) d = d.substring(0, maxChars - 2) + "..";
  gfx.setCursor(x, y);
  gfx.print(d);

  if (showBar) {
    drawMiniBar(gfx, 196, y, 34, 7, barPercent);
  }
  if (rightText.length() > 0) {
    printBold(gfx, 286 - (int)rightText.length() * 6, y, rightText);
  }
}

void drawDisplay1Data(const SparkData &spark, const Ai9700Data &ai) {
  display.setFont();
  display.setTextColor(GxEPD_BLACK);

  // -----------------------------------------------------------------------
  // CARD 1: SPARK LOCAL (x=0, y=0, w=296, h=66, r=4)
  // -----------------------------------------------------------------------
  display.drawRoundRect(0, 0, 296, 66, 4, GxEPD_BLACK);
  display.drawLine(0, 16, 295, 16, GxEPD_BLACK); // เส้นคั่น Header ในการ์ด

  drawLocalAiHeader(display, 5, "SPARK LOCAL", spark.lastUpdated, spark.connected,
                    spark.status, spark.totalTokens, "10.104.1.23");

  if (spark.connected) {
    // Row 1: Host (y = 21)
    drawHostRow(display, 21, spark.ram, spark.ramRatio, spark.cpu, spark.todayTokens, spark.speed);

    // Row 2-4: Containers (y = 32, 43, 54)
    const int containerY[3] = {32, 43, 54};
    for (int i = 0; i < spark.containerCount && i < 3; i++) {
      String name = spark.containers[i].name;
      if (name.startsWith("ollama-")) name = name.substring(7); // แสดงชื่อโมเดลเพียวๆ
      drawUsageRow(display, containerY[i], "", name, spark.containers[i].running,
                   spark.containers[i].cpuVal, spark.containers[i].cpuStr);
    }

    if (spark.containerCount == 0) {
      display.setCursor(8, 32);
      display.print("Waiting for container stats...");
    }
  } else {
    printBold(display, 8, 32, "Status: Offline / Host Unreachable");
    display.setCursor(8, 43);
    display.print("Check Ollama or Local AI Server");
  }

  // -----------------------------------------------------------------------
  // CARD 2: AI9700 (x=0, y=73, w=296, h=55, r=4)
  // -----------------------------------------------------------------------
  display.drawRoundRect(0, 73, 296, 55, 4, GxEPD_BLACK);
  display.drawLine(0, 89, 295, 89, GxEPD_BLACK); // เส้นคั่น Header ในการ์ด

  // SSH ได้แต่ llama-server ไม่ตอบ (status Offline) ให้ขึ้น -X เหมือนเครื่องปิด
  bool aiUp = ai.connected && ai.status != "Offline";
  drawLocalAiHeader(display, 78, "AI9700", ai.lastUpdated, aiUp, ai.status,
                    ai.totalTokens, "10.104.1.24");

  if (ai.connected) {
    // Row 1: Host (y = 94)
    drawHostRow(display, 94, ai.ram, ai.ramRatio, ai.cpu, ai.todayTokens, ai.speed);

    // Row 2: GPU use % + VRAM (y = 105)
    drawUsageRow(display, 105, "GPU", "VRAM " + ai.vramRatio, true, ai.gpu, String(ai.gpu) + "%");

    // Row 3: โมเดล + slot ที่กำลังตอบอยู่ (y = 116)
    if (ai.status == "Offline") {
      drawUsageRow(display, 116, "", "llama-server not responding", false, 0, "");
    } else if (ai.slotsTotal > 0) {
      drawUsageRow(display, 116, "", ai.model, true, (ai.slotsBusy * 100) / ai.slotsTotal,
                   "slot " + String(ai.slotsBusy) + "/" + String(ai.slotsTotal));
    } else {
      drawUsageRow(display, 116, "", ai.model, false, 0, "");
    }
  } else {
    printBold(display, 8, 94, "Status: Offline / Host Unreachable");
    display.setCursor(8, 105);
    display.print("Check AI9700 (SSH / llama-server)");
  }
}

// =========================================================================
// จอที่ 2: ANTIGRAVITY & CLAUDE CODE & CODEX (การ์ดกรอบมุมมน 3 ช่อง)
// =========================================================================
void updateDisplay2(const ClaudeCodeData &cc, const AntigravityData &ag, const CodexData &codex) {
  bool doFull = isFirstRender2 || partialCount2 >= fullRefreshEvery;

  if (doFull) {
    display2.setFullWindow();
    display2.firstPage();
    do {
      display2.fillScreen(GxEPD_WHITE);
      drawDisplay2Data(cc, ag, codex);
    } while (display2.nextPage());
    isFirstRender2 = false;
    partialCount2 = 0;
  } else {
    display2.setPartialWindow(0, 0, display2.width(), display2.height());
    display2.firstPage();
    do {
      display2.fillRect(0, 0, display2.width(), display2.height(), GxEPD_WHITE);
      drawDisplay2Data(cc, ag, codex);
    } while (display2.nextPage());
    partialCount2++;
  }
  display2.powerOff();

  prevCc = cc;
  prevAg = ag;
  prevCodex = codex;
}

void drawDisplay2Data(const ClaudeCodeData &cc, const AntigravityData &ag, const CodexData &codex) {
  display2.setFont();
  display2.setTextColor(GxEPD_BLACK);

  // -----------------------------------------------------------------------
  // เลย์เอาต์จอ 2 ใช้กฎเดียวกันทุกการ์ด: ทุกช่องว่างเท่ากัน G = 4px
  //   ขอบบน | G | หัวการ์ด (7) | G | เส้นคั่น | G | แถว (7) | G | แถว (7) | G | ขอบล่าง
  // ตัวอักษรฟอนต์มาตรฐานสูงจริง 7px (y ถึง y+6) บาร์สูง 7px วาดที่ y เดียวกับตัวอักษร
  // บาร์จึงอยู่กึ่งกลางตัวอักษรพอดี
  //   ANTIGRAVITY  y=0  h=55 (3 แถว)  ชิดขอบบนและซ้าย-ขวาของจอ
  //   CLAUDE CODE  y=59 h=33 (1 แถว)  ห่างการ์ดบน 4px
  //   OPENAI CODEX y=95 h=33 (1 แถว)  ห่างการ์ดบน 3px, จบที่ y=127 ชิดขอบล่างของจอ
  // -----------------------------------------------------------------------
  const int BAR_H = 7;

  // -----------------------------------------------------------------------
  // CARD 1: ANTIGRAVITY (x=0, y=0, w=296, h=55, r=4)
  // -----------------------------------------------------------------------
  display2.drawRoundRect(0, 0, 296, 55, 4, GxEPD_BLACK);
  display2.drawLine(0, 16, 295, 16, GxEPD_BLACK); // เส้นคั่น Header ในการ์ด

  // Header (y = 6)
  printBold(display2, 8, 5, "ANTIGRAVITY");

  if (ag.ideRunning) {
    if (ag.lastUpdated.length() > 0) {
      display2.setCursor(84, 5);
      display2.print(ag.lastUpdated);
    }

    // Row 1: Gemini (y = 22)
    printBold(display2, 8, 21, "Gemini");
    drawMiniBar(display2, 50, 21, 40, BAR_H, ag.geminiWeekly);
    printBold(display2, 94, 21, String(ag.geminiWeekly) + "%");
    display2.setCursor(122, 21);
    display2.print(ag.geminiWeeklyReset);

    printBold(display2, 160, 21, "5H");
    drawMiniBar(display2, 176, 21, 40, BAR_H, ag.gemini5Hr);
    printBold(display2, 220, 21, String(ag.gemini5Hr) + "%");
    display2.setCursor(248, 21);
    display2.print(ag.gemini5HrReset);

    // Row 2: Claude & GPT (y = 33)
    printBold(display2, 8, 32, "Claude");
    drawMiniBar(display2, 50, 32, 40, BAR_H, ag.claudeWeekly);
    printBold(display2, 94, 32, String(ag.claudeWeekly) + "%");
    display2.setCursor(122, 32);
    display2.print(ag.claudeWeeklyReset);

    printBold(display2, 160, 32, "5H");
    drawMiniBar(display2, 176, 32, 40, BAR_H, ag.claude5Hr);
    printBold(display2, 220, 32, String(ag.claude5Hr) + "%");
    display2.setCursor(248, 32);
    display2.print(ag.claude5HrReset);

    // Row 3: Gemini 4 Argon (y = 44) - มีเฉพาะใน Antigravity รุ่นใหม่ ไม่มีก็ไม่วาดแถวนี้
    if (ag.argonWeekly >= 0) {
      printBold(display2, 8, 43, "Argon");
      drawMiniBar(display2, 50, 43, 40, BAR_H, ag.argonWeekly);
      printBold(display2, 94, 43, String(ag.argonWeekly) + "%");
      display2.setCursor(122, 43);
      display2.print(ag.argonWeeklyReset);
    }
  } else {
    printBold(display2, 84, 5, "[CLOSED]");
    printBold(display2, 8, 21, "IDE Not Running / Closed");
    display2.setCursor(8, 32);
    display2.print("Open Antigravity IDE to view live quota");
  }
  drawWiFiIconEink(display2, 272, 1);

  // -----------------------------------------------------------------------
  // CARD 2: CLAUDE CODE (x=0, y=59, w=296, h=33, r=4)
  // -----------------------------------------------------------------------
  display2.drawRoundRect(0, 59, 296, 33, 4, GxEPD_BLACK);
  display2.drawLine(0, 75, 295, 75, GxEPD_BLACK); // เส้นคั่น Header ในการ์ด

  // Header (y = 63)
  printBold(display2, 8, 64, "CLAUDE CODE");
  if (cc.lastUpdated.length() > 0) {
    display2.setCursor(84, 64);
    display2.print(cc.lastUpdated);
  }

  String ccPlan = formatPlanName(cc.planType);
  if (ccPlan.length() > 0) {
    printBold(display2, 142, 64, "[" + ccPlan + "]");
  }

  if (cc.rateLimited) {
    drawRateLimitBanner(display2, 190, 62, cc.rateLimitReset);
  }

  // Row 1: Plan Limits (y = 79)
  printBold(display2, 8, 80, "Limit");
  drawMiniBar(display2, 48, 80, 40, BAR_H, cc.weekly);
  printBold(display2, 92, 80, String(cc.weekly) + "%");
  display2.setCursor(120, 80);
  display2.print(cc.weeklySub);

  printBold(display2, 160, 80, "5H");
  drawMiniBar(display2, 176, 80, 40, BAR_H, cc.fiveHr);
  printBold(display2, 220, 80, String(cc.fiveHr) + "%");
  display2.setCursor(248, 80);
  display2.print(cc.fiveHrSub);

  // -----------------------------------------------------------------------
  // CARD 3: OPENAI CODEX (x=0, y=95, w=296, h=33, r=4)
  // -----------------------------------------------------------------------
  display2.drawRoundRect(0, 95, 296, 33, 4, GxEPD_BLACK);
  display2.drawLine(0, 111, 295, 111, GxEPD_BLACK); // เส้นคั่น Header ในการ์ด

  // Header (y = 98)
  printBold(display2, 8, 100, "OPENAI CODEX");
  if (codex.lastUpdated.length() > 0) {
    display2.setCursor(88, 100);
    display2.print(codex.lastUpdated);
  }

  String codexPlan = formatPlanName(codex.planType);
  if (codexPlan.length() > 0) {
    printBold(display2, 142, 100, "[" + codexPlan + "]");
  }

  if (codex.rateLimited) {
    drawRateLimitBanner(display2, 190, 98, codex.rateLimitReset);
  }

  // Row 1: Weekly Limit (y = 114)
  printBold(display2, 8, 116, "Weekly");
  bool hasPrimaryQuota = codex.primaryPercent >= 0 &&
                         !(codex.primaryPercent == 100 && codex.primaryReset.length() == 0);
  if (codex.connected && hasPrimaryQuota) {
    drawMiniBar(display2, 52, 116, 60, BAR_H, codex.primaryPercent);
    printBold(display2, 118, 116, String(codex.primaryPercent) + "%");
    display2.setCursor(154, 116);
    display2.print(codex.primaryReset);
  } else if (codex.connected) {
    display2.setCursor(52, 116);
    display2.print("Syncing usage...");
  } else {
    printBold(display2, 52, 116, "Not Connected");
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
