# AIToken-eInk 📊

An ESP8266-based real-time AI Quota monitor using a 2.9" 3-color (Black/White/Red) or standard B/W E-Paper (E-ink) display. It periodically fetches usage quotas (e.g. Gemini, Claude, GPT models) from an API endpoint and visualizes remaining percentage bars and reset countdowns.

---

## ✨ Features

- **E-Paper Display Support**: Optimized for 2.9" 3-color (ZJYE290S08ROG01 / GDEM029C90) or 2-color B/W e-Paper displays via `GxEPD2`.
- **Low Power & Flicker-Free**: Supports selective Partial Refresh to update only modified bars without full-screen blinking.
- **Visual Alerts**: Highlights low quota (<10%) in **Red** (on 3-color screens).
- **Wi-Fi Connectivity**: Automatically reconnects to Wi-Fi and shows status icon.
- **OpenAI Codex CLI Monitoring**: Shows the weekly quota window returned by Codex with reset countdown and the subscribed plan badge (`[PLUS]`, `[PRO]`, `[FREE]`) on Display 1.
- **SPARK Local AI & Docker Containers Monitoring**: Displays local Ollama model name, status (`Ready`/`Active`/`Offline`), all-time total tokens (`All: 22.5M`), host machine RAM / CPU telemetry, active Docker container stats with mini bars, and today's token usage with inference speed on Display 1.
- **Claude Code CLI Monitoring**: Shows the subscribed plan badge, plan limits (Weekly / 5-Hour remaining + reset time), and token usage for today and the 5-Hour window on Display 2.
- **Antigravity / AI Token Monitoring**: Tracks Weekly and 5-Hour limits for Gemini and Claude / GPT models on Display 2.
- **Rate Limit Alerts**: 429 warnings on Claude Code and Codex with countdown timer.
- **Smart Partial Refresh**: Updates only changed values without full-screen flicker, with automatic full refresh every 30 updates to eliminate ghosting.

---

## 🛠️ Hardware Requirements

- **Microcontroller**: NodeMCU V3 / ESP8266 (or ESP32 with pin adjustments)
- **Displays**: Dual 2.9" E-ink Displays (SPI Interface)
  - Display 1: OpenAI Codex + SPARK Local (Containers & Host Telemetry)
  - Display 2: Claude Code + Antigravity IDE

### Pin Mapping (NodeMCU V3 ESP8266 -> E-Paper SPI)

| E-Paper Pin | NodeMCU Pin | GPIO | Description |
| :--- | :--- | :--- | :--- |
| **BUSY (Disp 1)** | `D2` | GPIO4 | Busy Status (Display 1) |
| **CS (Disp 1)** | `D8` | GPIO15 | Chip Select (Display 1) |
| **BUSY (Disp 2)** | `D6` | GPIO12 | Busy Status (Display 2) |
| **CS (Disp 2)** | `D1` | GPIO5 | Chip Select (Display 2) |
| **RST (Shared)** | `D4` | GPIO2 | Shared Reset |
| **DC (Shared)** | `D3` | GPIO0 | Shared Data / Command |
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
const unsigned long refreshInterval = 20000; // Interval in ms (e.g. 20 seconds)
```

### Expected API JSON Format

```json
{
  "geminiWeekly": 94,
  "geminiWeeklyReset": "5d 16h",
  "gemini5Hr": 100,
  "gemini5HrReset": "4h 12m",
  "claudeWeekly": 100,
  "claudeWeeklyReset": "6d 1h",
  "claude5Hr": 100,
  "claude5HrReset": "4h 23m",
  "lastUpdated": "08:09:28",

  "sparkConnected": true,
  "sparkModel": "gemma4-26b-uncensored",
  "sparkStatus": "Ready",
  "sparkCpu": 1,
  "sparkRam": 75,
  "sparkRamRatio": "92G/128G",
  "sparkTotalTokens": "22.5M",
  "sparkTodayTokens": "21k",
  "sparkSpeed": "55 t/s",
  "sparkContainers": [
    { "name": "LLM", "cpu": 45, "running": true },
    { "name": "ComfyUI", "cpu": 12, "running": true }
  ],
  "sparkLastUpdated": "08:09:18",

  "ccWeekly": 67,
  "ccPlanType": "max",
  "ccWeeklyReset": "2d 13h",
  "cc5Hr": 98,
  "cc5HrReset": "4h 51m",
  "ccTodayTokens": "3.96M",
  "ccTodayCache": "3.73M",
  "ccWindowTokens": "1.09M",
  "ccWindowCache": "1.01M",
  "ccRateLimited": false,
  "ccRateLimitReset": "",
  "ccLastUpdated": "08:07:38",

  "codexConnected": true,
  "codexPlanType": "plus",
  "codexPrimaryPercent": 92,
  "codexPrimaryReset": "4h 12m",
  "codexSecondaryPercent": 58,
  "codexSecondaryReset": "4d 6h",
  "codexRateLimited": false,
  "codexRateLimitReset": "",
  "codexLastUpdated": "08:09:15"
}
```

---

## 📄 License

MIT License
