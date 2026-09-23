# AIToken-eInk 📊

An ESP8266-based real-time AI Quota monitor using a 2.9" 3-color (Black/White/Red) or standard B/W E-Paper (E-ink) display. It periodically fetches usage quotas (e.g. Gemini, Claude, GPT models) from an API endpoint and visualizes remaining percentage bars and reset countdowns.

---

## ✨ Features

- **E-Paper Display Support**: Optimized for 2.9" 3-color (ZJYE290S08ROG01 / GDEM029C90) or 2-color B/W e-Paper displays via `GxEPD2`.
- **Low Power & Flicker-Free**: Supports selective Partial Refresh to update only modified bars without full-screen blinking.
- **Visual Alerts**: Highlights low quota (<10%) in **Red** (on 3-color screens).
- **Wi-Fi Connectivity**: Automatically reconnects to Wi-Fi and shows status icon.
- **Antigravity / AI Token Monitoring**: Tracks Weekly and 5-Hour limits for Gemini and Claude / GPT models.
- **Second Display – Claude Code**: Shows Claude Code plan limits (Weekly / 5-Hour remaining + reset time) and token usage for today and the current 5-hour window (in / out / cache). Data comes from the same bridge server (`AIToken/bridge_server.js`, started with `start_bridge.bat`).

---

## 🛠️ Hardware Requirements

- **Microcontroller**: NodeMCU V3 / ESP8266 (or ESP32 with pin adjustments)
- **Display**: 2.9" E-ink Display (SPI Interface)

### Pin Mapping (NodeMCU V3 ESP8266 -> E-Paper SPI)

| E-Paper Pin | NodeMCU Pin | GPIO | Description |
| :--- | :--- | :--- | :--- |
| **BUSY** | `D2` | GPIO4 | Busy Status |
| **RST** | `D4` | GPIO2 | Reset |
| **DC** | `D3` | GPIO0 | Data / Command |
| **CS** | `D8` | GPIO15 | Chip Select |
| **CLK (SCK)** | `D5` | GPIO14 | SPI Clock |
| **DIN (MOSI)** | `D7` | GPIO13 | SPI MOSI Data |
| **GND** | `G` | GND | Ground |
| **VCC** | `3V3` | 3.3V | Power Supply (3.3V) |

---

## 📦 Required Arduino Libraries

Install the following libraries via the Arduino IDE Library Manager:

1. **GxEPD2** by Jean-Marc Zingg
2. **Adafruit GFX Library** by Adafruit
3. **ArduinoJson** (v7 recommended) by Benoit Blanchon
4. **ESP8266WiFi** & **ESP8266HTTPClient** (included with ESP8266 core)

---

## ⚙️ Configuration

Open `AIToken-eInk.ino` and update the configuration section:

```cpp
// Wi-Fi Configuration
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";

// API Endpoint
const char *apiUrl = "http://<YOUR_SERVER_IP>:5000/api/quota";
const unsigned long refreshInterval = 60000; // Interval in ms (e.g. 1 minute)
```

### Expected API JSON Format

```json
{
  "geminiWeekly": 85,
  "geminiWeeklySubtext": "4d left",
  "gemini5Hr": 100,
  "gemini5HrSubtext": "4h left",
  "claudeWeekly": 42,
  "claudeWeeklySubtext": "2d left",
  "claude5Hr": 8,
  "claude5HrSubtext": "45m left",

  "ccWeekly": 57,
  "ccWeeklyReset": "2d 23h",
  "cc5Hr": 97,
  "cc5HrReset": "4h 51m",
  "ccTodayTokens": "3.96M", "ccTodayIn": "160k", "ccTodayOut": "64k", "ccTodayCache": "3.73M",
  "ccWindowTokens": "1.09M", "ccWindowIn": "63k", "ccWindowOut": "11k", "ccWindowCache": "1.01M"
}
```

---

## 📄 License

MIT License
