/*
 * ESP32 iOS AutoSwipe
 * -------------------
 * Turns an ESP32 into a BLE mouse that presses a mouse button at random
 * intervals. On the iPhone, AssistiveTouch maps that button to an action
 * (a recorded swipe gesture, Scroll Down, etc.).
 *
 * 讓 ESP32 變成藍牙滑鼠，每隔隨機時間按一次按鈕；
 * 在 iPhone 的 AssistiveTouch 把該按鈕綁定到動作（自訂滑動手勢、向下捲動等）。
 *
 * Requires / 需求:
 *   - ESP32 Arduino core 3.x (not ESP32-S2 / ESP32-P4)
 *   - NimBLE-Arduino >= 2.3.8
 *   - HijelHID_BLEMouse  https://github.com/HijelHub/HijelHID_BLEMouse
 *
 * Written with the help of Claude (Anthropic).
 * 本專案由 Claude (Anthropic) 協助撰寫。
 */

#include <HijelHID_BLEMouse.h>
#include <Preferences.h>

HijelBLEMouse mouse;
Preferences prefs;

// ===== Hardware / 硬體 =====
const int PAUSE_BUTTON_PIN = 0;     // BOOT button on most ESP32 boards / 多數開發板的 BOOT 鍵

// ===== Settings, editable over Serial and saved to flash =====
// ===== 可由序列埠修改並自動儲存的設定 =====
struct Config {
    uint32_t minMs  = 29000;        // Min interval / 最短間隔
    uint32_t maxMs  = 39000;        // Max interval / 最長間隔
    uint8_t  button = 0;            // 0=back 1=forward 2=middle
    uint16_t holdMs = 50;           // Button hold time / 按鈕按住時間
};
Config cfg;

const char* BUTTON_NAMES[] = {"back", "forward", "middle"};

// ===== Runtime state / 執行狀態 =====
bool paused = false;
bool bindMode = false;
uint32_t nextSwipeAt = 0;
uint32_t readyAt = 0;
uint32_t lastBindClick = 0;

// ---------- Settings storage / 設定存取 ----------
void loadConfig() {
    prefs.begin("swipe", true);
    cfg.minMs   = prefs.getUInt("minMs", cfg.minMs);
    cfg.maxMs   = prefs.getUInt("maxMs", cfg.maxMs);
    cfg.button  = prefs.getUChar("btn", cfg.button);
    cfg.holdMs  = prefs.getUShort("hold", cfg.holdMs);
    prefs.end();

    if (cfg.button > 2) cfg.button = 0;
    if (cfg.maxMs < cfg.minMs) cfg.maxMs = cfg.minMs;
}

void saveConfig() {
    prefs.begin("swipe", false);
    prefs.putUInt("minMs", cfg.minMs);
    prefs.putUInt("maxMs", cfg.maxMs);
    prefs.putUChar("btn", cfg.button);
    prefs.putUShort("hold", cfg.holdMs);
    prefs.end();
}

MouseButton currentButton() {
    switch (cfg.button) {
        case 1:  return MouseButton::Forward;
        case 2:  return MouseButton::Middle;
        default: return MouseButton::Back;
    }
}

// ---------- Actions / 動作 ----------
void scheduleNextSwipe() {
    uint32_t wait = random(cfg.minMs, cfg.maxMs + 1);
    nextSwipeAt = millis() + wait;
    Serial.printf("Next swipe in %.1f s\n", wait / 1000.0);
}

void setPaused(bool p) {
    paused = p;
    Serial.println(p ? ">> PAUSED" : ">> RESUMED");
    if (!p) scheduleNextSwipe();        // Restart the timer on resume / 繼續後重新計時
}

void setBindMode(bool on) {
    bindMode = on;
    if (on) {
        Serial.println(">> BIND MODE ON (sends the button every 2 s; type 'bind off' when done)");
    } else {
        Serial.println(">> BIND MODE OFF");
        scheduleNextSwipe();
    }
}

void triggerSwipe() {
    if (!mouse.isPaired()) {
        Serial.println("Not paired yet, cannot send.");
        return;
    }
    Serial.println("Swipe!");
    mouse.click(currentButton(), cfg.holdMs);
}

// ---------- Help and status / 說明與狀態 ----------
void printHelp() {
    Serial.println(
        "\n============ Commands ============\n"
        "help / h / ?         Show this help\n"
        "status / s           Show state and settings\n"
        "\n[Control]\n"
        "p                    Toggle pause / resume\n"
        "pause / resume       Explicit pause / resume\n"
        "swipe / b            Send one swipe now\n"
        "bind / k [on|off]    Bind mode (lets iOS detect the button)\n"
        "\n[Settings, saved automatically]\n"
        "interval <min> <max> Swipe interval in seconds, e.g. interval 25 45\n"
        "button <name>        back / forward / middle\n"
        "hold <ms>            Button hold time, 10-500\n"
        "reset                Restore defaults\n"
        "\nBOOT button: pause / resume\n"
        "==================================");
}

void printStatus() {
    Serial.println("\n---------- Status ----------");
    Serial.printf("Link:     %s\n", mouse.isPaired() ? "paired" : "not paired");
    Serial.printf("Mode:     %s\n", bindMode ? "bind mode" : (paused ? "paused" : "running"));
    Serial.printf("Interval: %.1f - %.1f s\n", cfg.minMs / 1000.0, cfg.maxMs / 1000.0);
    Serial.printf("Button:   %s (hold %u ms)\n", BUTTON_NAMES[cfg.button], cfg.holdMs);
    if (mouse.isPaired() && !paused && !bindMode) {
        int32_t left = (int32_t)(nextSwipeAt - millis());
        if (left > 0) Serial.printf("Next swipe in %.1f s\n", left / 1000.0);
    }
    Serial.println("----------------------------");
}

// ---------- Command parser / 指令解析 ----------
bool is(const char* c, const char* a, const char* b = nullptr) {
    return strcmp(c, a) == 0 || (b && strcmp(c, b) == 0);
}

void handleCommand(char* line) {
    char* cmd = strtok(line, " \t");
    if (!cmd) return;
    for (char* p = cmd; *p; p++) *p = tolower((unsigned char)*p);
    char* a1 = strtok(nullptr, " \t");
    char* a2 = strtok(nullptr, " \t");

    if (is(cmd, "help", "h") || is(cmd, "?")) {
        printHelp();

    } else if (is(cmd, "status", "s")) {
        printStatus();

    } else if (is(cmd, "p")) {
        setPaused(!paused);

    } else if (is(cmd, "pause")) {
        setPaused(true);

    } else if (is(cmd, "resume")) {
        setPaused(false);

    } else if (is(cmd, "swipe", "b")) {
        triggerSwipe();

    } else if (is(cmd, "bind", "k")) {
        setBindMode(a1 ? (strcasecmp(a1, "off") != 0) : !bindMode);

    } else if (is(cmd, "interval", "i")) {
        if (!a1 || !a2) { Serial.println("Usage: interval <min_s> <max_s>"); return; }
        float mn = atof(a1), mx = atof(a2);
        if (mn < 1 || mx < mn) { Serial.println("Error: need min >= 1 and max >= min"); return; }
        cfg.minMs = (uint32_t)(mn * 1000);
        cfg.maxMs = (uint32_t)(mx * 1000);
        saveConfig();
        Serial.printf("Interval set to %.1f - %.1f s\n", mn, mx);
        if (!paused && !bindMode) scheduleNextSwipe();

    } else if (is(cmd, "button")) {
        if (!a1) { Serial.println("Usage: button <back|forward|middle>"); return; }
        int found = -1;
        for (int i = 0; i < 3; i++) if (strcasecmp(a1, BUTTON_NAMES[i]) == 0) found = i;
        if (found < 0) { Serial.println("Error: must be back / forward / middle"); return; }
        cfg.button = found;
        saveConfig();
        Serial.printf("Button set to %s (re-bind it in iOS)\n", BUTTON_NAMES[found]);

    } else if (is(cmd, "hold")) {
        if (!a1) { Serial.println("Usage: hold <ms>"); return; }
        int v = atoi(a1);
        if (v < 10 || v > 500) { Serial.println("Error: range is 10-500"); return; }
        cfg.holdMs = v;
        saveConfig();
        Serial.printf("Hold time set to %d ms\n", v);

    } else if (is(cmd, "reset")) {
        cfg = Config();
        saveConfig();
        Serial.println("Defaults restored.");

    } else {
        Serial.printf("Unknown command: %s (type 'help')\n", cmd);
    }
}

// ---------- Input handling / 輸入處理 ----------
void readSerial() {
    static char buf[64];
    static uint8_t len = 0;
    static uint32_t lastRx = 0;

    while (Serial.available()) {
        char c = Serial.read();
        lastRx = millis();
        if (c == '\r' || c == '\n') {
            if (len) { buf[len] = 0; len = 0; handleCommand(buf); }
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = c;
        }
    }

    // If the Serial Monitor is set to "No line ending", treat a 100 ms pause
    // as end of input. / 若序列埠監控視窗設成「沒有行結尾」，停頓 100ms 視為輸入完成。
    if (len && millis() - lastRx > 100) {
        buf[len] = 0;
        len = 0;
        handleCommand(buf);
    }
}

void checkButton() {
    static bool lastBtn = HIGH;
    static uint32_t lastChange = 0;
    bool btn = digitalRead(PAUSE_BUTTON_PIN);
    if (btn != lastBtn && millis() - lastChange > 50) {   // Debounce / 防彈跳
        lastChange = millis();
        lastBtn = btn;
        if (btn == LOW) setPaused(!paused);
    }
}

// ---------- Main / 主程式 ----------
void setup() {
    Serial.begin(115200);
    pinMode(PAUSE_BUTTON_PIN, INPUT_PULLUP);
    randomSeed(esp_random());
    loadConfig();

    mouse.setUpdateRate(HIDRate::Hz125);
    mouse.begin();

    Serial.println("\nBLE Mouse started, waiting for iPhone pairing...");
    Serial.println("Type 'help' for all commands.");
}

void loop() {
    static bool lastPaired = false;

    readSerial();
    checkButton();

    bool paired = mouse.isPaired();
    if (paired != lastPaired) {
        lastPaired = paired;
        Serial.println(paired ? "iPhone paired!" : "iPhone disconnected.");
        if (paired) {
            readyAt = millis() + 1000;      // Non-blocking 1 s settle time / 非阻塞等待 1 秒
            scheduleNextSwipe();
        }
    }

    if (!paired || (int32_t)(millis() - readyAt) < 0) return;

    // Bind mode: send the button every 2 s so iOS can detect it.
    // 綁定模式：每 2 秒送一次按鈕，讓 iOS 辨識。
    if (bindMode) {
        if (millis() - lastBindClick > 2000) {
            lastBindClick = millis();
            mouse.click(currentButton(), max<uint16_t>(cfg.holdMs, 80));
            Serial.println("Bind click sent");
        }
        return;
    }

    if (paused) return;

    if ((int32_t)(millis() - nextSwipeAt) >= 0) {
        triggerSwipe();
        scheduleNextSwipe();
    }
}
