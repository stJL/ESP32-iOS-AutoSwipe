#include <HijelHID_BLEMouse.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>

HijelBLEMouse mouse;
Preferences prefs;

// ===== Wi-Fi 設定 =====
WiFiManager wifiManager;

const char* CONFIG_AP_NAME = "AutoSwipe-Setup";
const char* CONFIG_AP_PASSWORD = "SwipeSetup27";

const uint32_t CONFIG_PORTAL_TIMEOUT = 180;

// 請自行換成一組較長的隨機字串
const char* API_KEY = "REPLACE_WITH_YOUR_OWN_RANDOM_API_KEY";  // 請在燒錄前換成自己的隨機金鑰，勿公開

WebServer server(80);

bool httpStarted = false;
uint32_t lastWifiAttempt = 0;

const char* requestHeaders[] = {"X-API-Key", "Cookie"};

// 瀏覽器信任設定：最多 4 個裝置，記錄存在獨立 NVS namespace，重開機後保留。
const uint8_t MAX_TRUSTED_DEVICES = 4;
const char* SESSION_COOKIE = "AutoSwipeSession";
const uint32_t SESSION_COOKIE_MAX_AGE = 30UL * 24UL * 60UL * 60UL;

const int PAUSE_BUTTON_PIN = 0;     // BOOT 鍵

// ===== 可在序列埠修改的設定（自動儲存）=====
struct Config {
    uint32_t minMs  = 25000;        // 最短間隔
    uint32_t maxMs  = 30000;        // 最長間隔
    uint8_t  button = 0;            // 0=back 1=forward 2=middle
    uint16_t holdMs = 50;           // 按鈕按住時間
};
Config cfg;

// 五組設定檔獨立存放於 swipe-prof，不影響既有 swipe / swipe-auth 資料。
const uint8_t PROFILE_COUNT = 5;
struct Profile {
    String name;
    uint32_t minMs;
    uint32_t maxMs;
    uint8_t button;
    uint16_t holdMs;
};
Profile profiles[PROFILE_COUNT];
int8_t activeProfile = -1; // -1 代表目前使用自訂設定
bool runtimeTemporary = false; // true = 本次運作的臨時設定，重開機會還原

const char* BUTTON_NAMES[] = {"back", "forward", "middle"};

void setPaused(bool p);
void triggerSwipe();
void scheduleNextSwipe();
void setBindMode(bool on);
void loadProfiles();
void saveProfile(uint8_t slot, const Profile& profile);
void saveActiveProfile(int8_t slot);

// ===== 執行狀態 =====
bool paused = false;
bool bindMode = false;
uint32_t nextSwipeAt = 0;
uint32_t readyAt = 0;
uint32_t lastBindClick = 0;

// ---------- 設定存取 ----------
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

Profile defaultProfile(uint8_t slot) {
    Profile p;
    p.name = String("Profile ") + String(slot + 1);
    p.minMs = 25000;
    p.maxMs = 30000;
    p.button = 0;
    p.holdMs = 50;
    return p;
}

void saveProfile(uint8_t slot, const Profile& profile) {
    if (slot >= PROFILE_COUNT) return;
    Preferences store;
    store.begin("swipe-prof", false);
    String suffix = String(slot);
    store.putString((String("n") + suffix).c_str(), profile.name);
    store.putUInt((String("mn") + suffix).c_str(), profile.minMs);
    store.putUInt((String("mx") + suffix).c_str(), profile.maxMs);
    store.putUChar((String("bt") + suffix).c_str(), profile.button);
    store.putUShort((String("hd") + suffix).c_str(), profile.holdMs);
    store.end();
    profiles[slot] = profile;
}

void saveActiveProfile(int8_t slot) {
    activeProfile = slot;
    runtimeTemporary = false;
    Preferences store;
    store.begin("swipe-prof", false);
    store.putChar("active", slot);
    store.end();
}

void loadProfiles() {
    Preferences store;
    store.begin("swipe-prof", false);
    bool initialized = store.getBool("init", false);
    if (!initialized) {
        // 第一次升級時，把原本正在使用的設定保存在 Profile 1，避免舊設定遺失。
        Profile first;
        first.name = "Profile 1";
        first.minMs = cfg.minMs;
        first.maxMs = cfg.maxMs;
        first.button = cfg.button;
        first.holdMs = cfg.holdMs;
        store.putString("n0", first.name);
        store.putUInt("mn0", first.minMs);
        store.putUInt("mx0", first.maxMs);
        store.putUChar("bt0", first.button);
        store.putUShort("hd0", first.holdMs);
        for (uint8_t i = 1; i < PROFILE_COUNT; ++i) {
            Profile d = defaultProfile(i);
            String x = String(i);
            store.putString((String("n") + x).c_str(), d.name);
            store.putUInt((String("mn") + x).c_str(), d.minMs);
            store.putUInt((String("mx") + x).c_str(), d.maxMs);
            store.putUChar((String("bt") + x).c_str(), d.button);
            store.putUShort((String("hd") + x).c_str(), d.holdMs);
        }
        store.putChar("active", 0);
        store.putBool("init", true);
        activeProfile = 0;
        profiles[0] = first;
        for (uint8_t i = 1; i < PROFILE_COUNT; ++i) profiles[i] = defaultProfile(i);
    } else {
        activeProfile = store.getChar("active", -1);
        if (activeProfile < -1 || activeProfile >= PROFILE_COUNT) activeProfile = -1;
        for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
            String x = String(i);
            Profile d = defaultProfile(i);
            profiles[i].name = store.getString((String("n") + x).c_str(), d.name);
            profiles[i].minMs = store.getUInt((String("mn") + x).c_str(), d.minMs);
            profiles[i].maxMs = store.getUInt((String("mx") + x).c_str(), d.maxMs);
            profiles[i].button = store.getUChar((String("bt") + x).c_str(), d.button);
            profiles[i].holdMs = store.getUShort((String("hd") + x).c_str(), d.holdMs);
            if (profiles[i].button > 2) profiles[i].button = 0;
            if (profiles[i].minMs < 1000) profiles[i].minMs = 1000;
            if (profiles[i].maxMs < profiles[i].minMs) profiles[i].maxMs = profiles[i].minMs;
            if (profiles[i].maxMs > 3600000UL) profiles[i].maxMs = 3600000UL;
            if (profiles[i].holdMs < 10 || profiles[i].holdMs > 500) profiles[i].holdMs = 50;
        }
        if (activeProfile >= 0) {
            cfg.minMs = profiles[activeProfile].minMs;
            cfg.maxMs = profiles[activeProfile].maxMs;
            cfg.button = profiles[activeProfile].button;
            cfg.holdMs = profiles[activeProfile].holdMs;
        }
    }
    store.end();
}

String jsonEscape(const String& value) {
    String out;
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if ((uint8_t)c >= 0x20) out += c;
    }
    return out;
}

String cleanProfileName(String name) {
    name.trim();
    String out;
    for (size_t i = 0; i < name.length() && out.length() < 30; ++i) {
        uint8_t c = (uint8_t)name[i];
        if (c >= 0x20 && c != 0x7f) out += (char)c;
    }
    out.trim();
    return out.length() ? out : String("Profile");
}

bool readProfileSettings(Profile& p) {
    if (!server.hasArg("button") || !server.hasArg("minSec") ||
        !server.hasArg("maxSec") || !server.hasArg("holdMs")) return false;
    String button = server.arg("button");
    button.toLowerCase();
    int index = -1;
    for (int i = 0; i < 3; ++i) if (button == BUTTON_NAMES[i]) index = i;
    long minSec = server.arg("minSec").toInt();
    long maxSec = server.arg("maxSec").toInt();
    long hold = server.arg("holdMs").toInt();
    if (index < 0 || minSec < 1 || maxSec < minSec || maxSec > 3600 || hold < 10 || hold > 500) return false;
    p.button = (uint8_t)index;
    p.minMs = (uint32_t)minSec * 1000UL;
    p.maxMs = (uint32_t)maxSec * 1000UL;
    p.holdMs = (uint16_t)hold;
    return true;
}

void applyProfileToRuntime(uint8_t slot, bool persist) {
    if (slot >= PROFILE_COUNT) return;
    cfg.minMs = profiles[slot].minMs;
    cfg.maxMs = profiles[slot].maxMs;
    cfg.button = profiles[slot].button;
    cfg.holdMs = profiles[slot].holdMs;
    if (persist) {
        saveConfig();
        saveActiveProfile((int8_t)slot);
    }
    if (!paused && !bindMode && mouse.isPaired()) scheduleNextSwipe();
}

MouseButton currentButton() {
    switch (cfg.button) {
        case 1:  return MouseButton::Forward;
        case 2:  return MouseButton::Middle;
        default: return MouseButton::Back;
    }
}

// ---------- 動作 ----------

// ---------- HTTP API ----------
bool checkApiKey() {
    if (!server.hasHeader("X-API-Key")) {
        server.send(401, "application/json",
                    "{\"error\":\"Unauthorized\"}");
        return false;
    }

    if (server.header("X-API-Key") != API_KEY) {
        server.send(403, "application/json",
                    "{\"error\":\"Invalid API key\"}");
        return false;
    }

    return true;
}

// ---------- 瀏覽器登入與持久信任 ----------
String sha256Hex(const String& input) {
    uint8_t digest[32];
    mbedtls_sha256((const unsigned char*)input.c_str(), input.length(), digest, 0);
    char out[65];
    for (int i = 0; i < 32; ++i) sprintf(out + i * 2, "%02x", digest[i]);
    out[64] = '\0';
    return String(out);
}

String makeSessionToken() {
    uint8_t randomBytes[32];
    esp_fill_random(randomBytes, sizeof(randomBytes));
    char out[65];
    for (int i = 0; i < 32; ++i) sprintf(out + i * 2, "%02x", randomBytes[i]);
    out[64] = '\0';
    return String(out);
}

String sessionFromCookie() {
    if (!server.hasHeader("Cookie")) return "";
    String cookies = server.header("Cookie");
    String key = String(SESSION_COOKIE) + "=";
    int start = cookies.indexOf(key);
    if (start < 0) return "";
    start += key.length();
    int end = cookies.indexOf(';', start);
    String token = end < 0 ? cookies.substring(start) : cookies.substring(start, end);
    token.trim();
    return token;
}

String trustedRecord(uint8_t slot) {
    Preferences auth;
    auth.begin("swipe-auth", true);
    String record = auth.getString((String("s") + slot).c_str(), "");
    auth.end();
    return record;
}

bool sessionSlotMatches(uint8_t slot, const String& tokenHash) {
    String record = trustedRecord(slot);
    int sep = record.indexOf('|');
    return sep == 64 && record.substring(0, sep) == tokenHash;
}

int currentSessionSlot() {
    String token = sessionFromCookie();
    if (token.length() != 64) return -1;
    String hash = sha256Hex(token);
    for (uint8_t i = 0; i < MAX_TRUSTED_DEVICES; ++i) {
        if (sessionSlotMatches(i, hash)) return i;
    }
    return -1;
}

bool requireSession() {
    if (currentSessionSlot() >= 0) return true;
    server.send(401, "application/json", "{\"error\":\"Login required\"}");
    return false;
}

// 僅在已登入的瀏覽器明確要求時回傳韌體內的 API Key。
void handleWebApiKey() {
    if (!requireSession()) return;
    String json = "{\"key\":\"" + jsonEscape(String(API_KEY)) + "\"}";
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.send(200, "application/json", json);
}

String cleanDeviceName(String name) {
    String clean;
    for (size_t i = 0; i < name.length() && clean.length() < 24; ++i) {
        char c = name[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == ' ' || c == '-' || c == '_') clean += c;
    }
    clean.trim();
    return clean.length() ? clean : String("Browser");
}

void handleWebLogin() {
    if (server.method() != HTTP_POST || server.arg("key") != API_KEY) {
        server.send(403, "application/json", "{\"error\":\"Invalid API key\"}");
        return;
    }

    int freeSlot = -1;
    for (uint8_t i = 0; i < MAX_TRUSTED_DEVICES; ++i) {
        if (trustedRecord(i).length() == 0) { freeSlot = i; break; }
    }
    if (freeSlot < 0) {
        server.send(409, "application/json", "{\"error\":\"Trusted device limit reached. Revoke a device first.\"}");
        return;
    }

    String token = makeSessionToken();
    String hash = sha256Hex(token);
    String device = cleanDeviceName(server.arg("device"));
    Preferences auth;
    auth.begin("swipe-auth", false);
    auth.putString((String("s") + freeSlot).c_str(), hash + "|" + device);
    auth.end();

    server.sendHeader("Set-Cookie", String(SESSION_COOKIE) + "=" + token +
        "; Max-Age=" + String(SESSION_COOKIE_MAX_AGE) +
        "; Path=/; HttpOnly; SameSite=Strict");
    server.send(200, "application/json", "{\"success\":true}");
}

void handleWebLogout() {
    int slot = currentSessionSlot();
    if (slot >= 0) {
        Preferences auth;
        auth.begin("swipe-auth", false);
        auth.remove((String("s") + slot).c_str());
        auth.end();
    }
    server.sendHeader("Set-Cookie", String(SESSION_COOKIE) + "=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict");
    server.send(200, "application/json", "{\"success\":true}");
}

void handleRevokeDevice() {
    if (!requireSession()) return;
    int slot = server.arg("slot").toInt();
    if (slot < 0 || slot >= MAX_TRUSTED_DEVICES || trustedRecord(slot).length() == 0) {
        server.send(400, "application/json", "{\"error\":\"Invalid device slot\"}");
        return;
    }
    Preferences auth;
    auth.begin("swipe-auth", false);
    auth.remove((String("s") + slot).c_str());
    auth.end();
    server.send(200, "application/json", "{\"success\":true}");
}

void handleRevokeAll() {
    if (!requireSession()) return;
    Preferences auth;
    auth.begin("swipe-auth", false);
    for (uint8_t i = 0; i < MAX_TRUSTED_DEVICES; ++i) auth.remove((String("s") + i).c_str());
    auth.end();
    server.sendHeader("Set-Cookie", String(SESSION_COOKIE) + "=; Max-Age=0; Path=/; HttpOnly; SameSite=Strict");
    server.send(200, "application/json", "{\"success\":true}");
}

void handleWebStatus() {
    if (!requireSession()) return;
    String json = "{\"paired\":" + String(mouse.isPaired() ? "true" : "false");
    json += ",\"paused\":" + String(paused ? "true" : "false");
    json += ",\"bindMode\":" + String(bindMode ? "true" : "false");
    json += ",\"button\":\"" + String(BUTTON_NAMES[cfg.button]) + "\"";
    json += ",\"holdMs\":" + String(cfg.holdMs);
    json += ",\"minMs\":" + String(cfg.minMs);
    json += ",\"maxMs\":" + String(cfg.maxMs);
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
    json += ",\"activeProfile\":" + String(activeProfile);
    json += ",\"temporary\":" + String(runtimeTemporary ? "true" : "false");
    json += ",\"profiles\":[";
    for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
        if (i) json += ",";
        json += "{\"slot\":" + String(i) + ",\"name\":\"" + jsonEscape(profiles[i].name) +
                "\",\"button\":\"" + String(BUTTON_NAMES[profiles[i].button]) +
                "\",\"minMs\":" + String(profiles[i].minMs) +
                ",\"maxMs\":" + String(profiles[i].maxMs) +
                ",\"holdMs\":" + String(profiles[i].holdMs) + "}";
    }
    json += "],\"devices\":[";
    bool first = true;
    int current = currentSessionSlot();
    for (uint8_t i = 0; i < MAX_TRUSTED_DEVICES; ++i) {
        String record = trustedRecord(i);
        int sep = record.indexOf('|');
        if (sep != 64) continue;
        if (!first) json += ",";
        first = false;
        String name = record.substring(sep + 1);
        name.replace("\\", "\\\\"); name.replace("\"", "\\\"");
        json += "{\"slot\":" + String(i) + ",\"name\":\"" + name +
                "\",\"current\":" + String(i == current ? "true" : "false") + "}";
    }
    json += "]}";
    server.send(200, "application/json", json);
}

void handleWebPause() { if (!requireSession()) return; setPaused(true); server.send(200, "application/json", "{\"success\":true}"); }
void handleWebResume() { if (!requireSession()) return; setPaused(false); server.send(200, "application/json", "{\"success\":true}"); }
void handleWebToggle() { if (!requireSession()) return; setPaused(!paused); server.send(200, "application/json", String("{\"success\":true,\"paused\":") + (paused ? "true}" : "false}")); }
void handleWebSwipe() { if (!requireSession()) return; triggerSwipe(); server.send(200, "application/json", "{\"success\":true}"); }

// 網頁設定：預設為臨時套用；persist=1 才寫入一般設定區。
void handleWebSettings() {
    if (!requireSession()) return;
    Profile values;
    if (!readProfileSettings(values)) {
        server.send(400, "application/json", "{\"error\":\"Invalid settings\"}");
        return;
    }
    cfg.button = values.button;
    cfg.minMs = values.minMs;
    cfg.maxMs = values.maxMs;
    cfg.holdMs = values.holdMs;
    bool persist = server.arg("persist") == "1";
    if (persist) {
        saveConfig();
        saveActiveProfile(-1);
    } else {
        activeProfile = -1;       // 只改執行中的標記，不覆寫 NVS 的開機 Profile
        runtimeTemporary = true;
    }
    if (!paused && !bindMode && mouse.isPaired()) scheduleNextSwipe();
    server.send(200, "application/json", String("{\"success\":true,\"persisted\":") + (persist ? "true}" : "false}"));
}

void handleWebBind() {
    if (!requireSession()) return;
    if (!server.hasArg("enabled")) { server.send(400, "application/json", "{\"error\":\"Missing enabled\"}"); return; }
    String v = server.arg("enabled");
    setBindMode(v == "1" || v == "true" || v == "on");
    server.send(200, "application/json", "{\"success\":true}");
}

void handleWebReset() {
    if (!requireSession()) return;
    cfg = Config();
    saveConfig();
    saveActiveProfile(-1);
    if (!paused && !bindMode && mouse.isPaired()) scheduleNextSwipe();
    server.send(200, "application/json", "{\"success\":true}");
}

void handleProfileApply() {
    if (!requireSession()) return;
    int slot = server.arg("slot").toInt();
    if (slot < 0 || slot >= PROFILE_COUNT) { server.send(400, "application/json", "{\"error\":\"Invalid profile\"}"); return; }
    applyProfileToRuntime((uint8_t)slot, true);
    server.send(200, "application/json", "{\"success\":true}");
}

void handleProfileSave() {
    if (!requireSession()) return;
    int slot = server.arg("slot").toInt();
    if (slot < 0 || slot >= PROFILE_COUNT) { server.send(400, "application/json", "{\"error\":\"Invalid profile\"}"); return; }
    Profile updated = profiles[slot];
    if (!readProfileSettings(updated)) { server.send(400, "application/json", "{\"error\":\"Invalid settings\"}"); return; }
    if (server.hasArg("name")) updated.name = cleanProfileName(server.arg("name"));
    saveProfile((uint8_t)slot, updated);
    // 若修改的是目前正在運作的 Profile，運作中的舊值不會被偷偷套用；
    // UI 改標示為臨時自訂，而 NVS 仍保留該 Profile 作為下次開機設定。
    if (activeProfile == slot &&
        (cfg.button != updated.button || cfg.minMs != updated.minMs ||
         cfg.maxMs != updated.maxMs || cfg.holdMs != updated.holdMs)) {
        activeProfile = -1;
        runtimeTemporary = true;
    }
    server.send(200, "application/json", "{\"success\":true}");
}

void handleProfileRename() {
    if (!requireSession()) return;
    int slot = server.arg("slot").toInt();
    if (slot < 0 || slot >= PROFILE_COUNT || !server.hasArg("name")) { server.send(400, "application/json", "{\"error\":\"Invalid profile/name\"}"); return; }
    Profile updated = profiles[slot];
    updated.name = cleanProfileName(server.arg("name"));
    saveProfile((uint8_t)slot, updated);
    server.send(200, "application/json", "{\"success\":true}");
}

void handleProfileReset() {
    if (!requireSession()) return;
    int slot = server.arg("slot").toInt();
    if (slot < 0 || slot >= PROFILE_COUNT) { server.send(400, "application/json", "{\"error\":\"Invalid profile\"}"); return; }
    Profile d = defaultProfile((uint8_t)slot);
    saveProfile((uint8_t)slot, d);
    if (activeProfile == slot &&
        (cfg.button != d.button || cfg.minMs != d.minMs ||
         cfg.maxMs != d.maxMs || cfg.holdMs != d.holdMs)) {
        activeProfile = -1;
        runtimeTemporary = true;
    }
    server.send(200, "application/json", "{\"success\":true}");
}

void handleProfilesResetAll() {
    if (!requireSession()) return;
    for (uint8_t i = 0; i < PROFILE_COUNT; ++i) {
        Profile d = defaultProfile(i);
        saveProfile(i, d);
    }
    // 不改變目前運作值；若原本選中某組 Profile，重設後改以自訂狀態呈現。
    if (activeProfile >= 0) {
        activeProfile = -1;
        runtimeTemporary = true;
    }
    server.send(200, "application/json", "{\"success\":true}");
}

const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="zh-Hant"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>AutoSwipe</title>
<style>body{font:16px system-ui,sans-serif;max-width:620px;margin:30px auto;padding:0 16px;background:#111827;color:#e5e7eb}h1{font-size:1.7rem}section{background:#1f2937;padding:18px;border-radius:14px;margin:14px 0}input,button{font:inherit;padding:11px;border-radius:8px;border:0}input{width:95%;margin:8px 0;background:#374151;color:white;box-sizing:border-box}button{margin:5px 4px 5px 0;cursor:pointer;background:#2563eb;color:white}label{display:block;margin:12px 0 4px;font-weight:600}select{font:inherit;padding:11px;border-radius:8px;border:0;background:#374151;color:white;width:100%;margin:4px 0}button.secondary{background:#4b5563}button.danger{background:#b91c1c}.muted{color:#9ca3af;font-size:.9rem}#message{min-height:1.4em;color:#fbbf24}.device,.profile{border-top:1px solid #374151;padding:10px 0}.profile{margin-top:8px}.profile small{color:#9ca3af}.profile-card{border:1px solid #374151;border-radius:10px;margin:10px 0;overflow:hidden}.profile-card summary{padding:14px;cursor:pointer;list-style:none;display:flex;align-items:center;gap:8px}.profile-card summary::-webkit-details-marker{display:none}.profile-card summary:after{content:"＋";margin-left:auto;color:#9ca3af}.profile-card[open] summary:after{content:"－"}.profile-body{padding:0 14px 14px}.profile-meta{color:#9ca3af;font-size:.88rem}.dirty{color:#fbbf24;font-size:.82rem}.profile-body input,.profile-body select{width:100%;box-sizing:border-box}#firmwareApiKey{flex:1 1 180px;width:auto;min-width:0}.actions{display:flex;flex-wrap:wrap;gap:4px}.actions button{flex:1 1 auto}.summary{white-space:pre-wrap;color:#fbbf24}</style></head><body>
<h1>AutoSwipe 控制面板</h1><p class="muted">同一個 Wi-Fi 網路內使用。首次登入需要 API Key；登入後會記住這個瀏覽器。</p><p id="message"></p>
<section id="login"><h2>裝置授權</h2><form id="loginForm"><input id="apiKey" type="password" placeholder="API Key" autocomplete="current-password" required><button type="submit">登入並信任此裝置</button></form><p class="muted">API Key 只用於首次授權，不會保存在瀏覽器。</p></section>
<div id="panel" hidden>
<section><h2>控制</h2><p id="state">讀取中…</p><div class="actions"><button onclick="action('pause')">暫停</button><button onclick="action('resume')">繼續</button><button onclick="action('toggle')">切換</button><button onclick="action('swipe')">立即滑動</button></div><div class="actions"><button class="secondary" onclick="setBind(true)">開啟 Bind Mode</button><button class="secondary" onclick="setBind(false)">關閉 Bind Mode</button></div><p class="muted">Bind Mode 每 2 秒送出一次目前選定的滑鼠按鍵，方便 iOS AssistiveTouch 辨識。</p></section>
<section><h2>目前設定</h2><p id="settings">—</p><p id="activeProfile" class="muted">—</p><p id="connection" class="muted"></p>
<h3>修改目前設定</h3><form id="settingsForm">
<label for="buttonSetting">滑鼠按鍵</label><select id="buttonSetting"><option value="back">Back</option><option value="forward">Forward</option><option value="middle">Middle</option></select>
<label for="minSetting">最短滑動間隔（秒）</label><input id="minSetting" type="number" min="1" max="3600" step="1" required>
<label for="maxSetting">最長滑動間隔（秒）</label><input id="maxSetting" type="number" min="1" max="3600" step="1" required>
<label for="holdSetting">按鍵持續時間（毫秒）</label><input id="holdSetting" type="number" min="10" max="500" step="1" required>
<div class="actions"><button type="button" onclick="applyTemporary()">臨時套用</button><button type="button" class="secondary" onclick="saveCustomSettings()">保存為自訂設定</button></div>
</form><p id="changeSummary" class="summary"></p><p class="muted">臨時套用只影響本次運作，重開機後恢復上次保存的設定。「保存為自訂設定」則會記住目前數值，但不會修改任何 Profile。</p>
<div class="actions"><button class="danger" onclick="resetCurrent()">Reset Settings（恢復目前設定預設值）</button></div></section>
<section><h2>Profiles</h2><p class="muted">每組 Profile 可獨立展開編輯。保存修改只更新該組設定，不會立即套用；「套用」才會切換目前運作設定，並在重開機後恢復。</p><div id="profiles"></div><button class="danger" onclick="resetAllProfiles()">Reset All Profiles</button></section>
<section><h2>iPhone 捷徑整合</h2><p class="muted">使用舊版 API（GET + X-API-Key），適合 iOS「取得 URL 內容」。</p><p id="shortcutBase"></p><label>韌體內的 API Key</label><div class="actions"><input id="firmwareApiKey" type="password" autocomplete="off" placeholder="尚未查看" readonly><button type="button" class="secondary" onclick="revealApiKey()">查看</button><button type="button" class="secondary" onclick="copyFirmwareApiKey()">複製</button></div><p class="muted">按下「查看」才會向 ESP32 要求金鑰。回傳內容會短暫留在此頁記憶體中；HTTP 不加密，請只在可信任的網路使用。已登入的瀏覽器使用者均可查看。</p><div id="shortcutLinks"></div><p class="muted">捷徑設定：新增「取得 URL 內容」→ 方法選 GET → 展開標頭（Headers）→ 新增 X-API-Key，值填入你的 API Key。URL 使用上方對應功能的網址。不要把 API Key 放進 URL 查詢參數。</p></section>
<section><h2>已信任的瀏覽器</h2><div id="devices"></div><button class="danger" onclick="logout()">撤銷此瀏覽器授權</button><button class="danger" onclick="revokeAll()">撤銷所有信任裝置</button></section>
</div>
<script>
const $=id=>document.getElementById(id),msg=t=>$('message').textContent=t;let latest=null;
let currentDirty=false,profileSignature='',openProfileSlot=-1,profileDirty=new Set(),firmwareKey='';
async function req(path,body){let post=body!==undefined;let r=await fetch(path,{method:post?'POST':'GET',headers:post?{'Content-Type':'application/x-www-form-urlencoded'}:{},body:post?(body||''):undefined,credentials:'same-origin',cache:'no-store'});let j={};try{j=await r.json()}catch(e){}if(!r.ok)throw Error(j.error||('HTTP '+r.status));return j;}
function enc(v){return encodeURIComponent(v)}
function browserName(){let u=navigator.userAgent;if(/iPhone|iPad|iPod/.test(u))return 'iPhone/iPad Safari';if(/Android/.test(u))return 'Android Browser';if(/Firefox/.test(u))return 'Firefox';if(/Edg/.test(u))return 'Edge';if(/Chrome/.test(u))return 'Chrome';if(/Safari/.test(u))return 'Safari';return 'Browser';}
function values(){return {button:$('buttonSetting').value,minSec:Number($('minSetting').value),maxSec:Number($('maxSetting').value),holdMs:Number($('holdSetting').value)}}
function validate(v){return Number.isInteger(v.minSec)&&Number.isInteger(v.maxSec)&&Number.isInteger(v.holdMs)&&v.minSec>=1&&v.maxSec>=v.minSec&&v.maxSec<=3600&&v.holdMs>=10&&v.holdMs<=500}
function formBody(v,extra=''){return 'button='+enc(v.button)+'&minSec='+v.minSec+'&maxSec='+v.maxSec+'&holdMs='+v.holdMs+(extra?'&'+extra:'')}
function currentValues(){return latest?{button:latest.button,minSec:latest.minMs/1000,maxSec:latest.maxMs/1000,holdMs:latest.holdMs}:null}
function summary(oldV,newV){let names={back:'Back',forward:'Forward',middle:'Middle'},a=[];if(oldV.button!==newV.button)a.push('按鍵：'+names[oldV.button]+' → '+names[newV.button]);if(oldV.minSec!==newV.minSec||oldV.maxSec!==newV.maxSec)a.push('間隔：'+oldV.minSec+'–'+oldV.maxSec+' 秒 → '+newV.minSec+'–'+newV.maxSec+' 秒');if(oldV.holdMs!==newV.holdMs)a.push('按住時間：'+oldV.holdMs+' ms → '+newV.holdMs+' ms');return a.length?a.join('；'):'設定沒有變更'}
function profileValues(slot){let root=document.querySelector('[data-profile="'+slot+'"]');return {name:root.querySelector('.p-name').value,button:root.querySelector('.p-button').value,minSec:Number(root.querySelector('.p-min').value),maxSec:Number(root.querySelector('.p-max').value),holdMs:Number(root.querySelector('.p-hold').value)}}
function profileDirtyMark(slot,on){if(on)profileDirty.add(slot);else profileDirty.delete(slot);let root=document.querySelector('[data-profile="'+slot+'"]');if(root){let badge=root.querySelector('.dirty');if(badge)badge.hidden=!on;}}
async function refresh(){try{let s=await req('/api/status');latest=s;$('login').hidden=true;$('panel').hidden=false;$('state').textContent='BLE：'+(s.paired?'已配對':'未配對')+'；狀態：'+(s.paused?'暫停中':'運作中')+(s.bindMode?'；Bind Mode':'');$('settings').textContent='按鈕：'+s.button+'；按住：'+s.holdMs+' ms；間隔：'+(s.minMs/1000)+'–'+(s.maxMs/1000)+' 秒';$('activeProfile').textContent='目前使用：'+(s.temporary?'臨時自訂（重開機後恢復已保存設定）':(s.activeProfile>=0?s.profiles[s.activeProfile].name+'（已保存）':'自訂設定（已保存）'));$('connection').textContent='IP：'+s.ip+'；網址：http://autoswipe.local/';
if(!currentDirty&&!['buttonSetting','minSetting','maxSetting','holdSetting'].includes(document.activeElement.id)){$('buttonSetting').value=s.button;$('minSetting').value=s.minMs/1000;$('maxSetting').value=s.maxMs/1000;$('holdSetting').value=s.holdMs;}
let sig=JSON.stringify({profiles:s.profiles,activeProfile:s.activeProfile});if(sig!==profileSignature&&!profileDirty.size){profileSignature=sig;renderProfiles(s);}
renderShortcuts(s);$('devices').innerHTML='';s.devices.forEach(d=>{let row=document.createElement('div');row.className='device';let label=document.createElement('span');label.textContent=d.name+(d.current?'（目前瀏覽器）':'');let b=document.createElement('button');b.className='danger';b.textContent='撤銷';b.onclick=()=>revoke(d.slot);row.append(label,b);$('devices').append(row);});}catch(e){clearFirmwareKey();$('panel').hidden=true;$('login').hidden=false;if(e.message!=='HTTP 401')msg(e.message);}}
function renderProfiles(s){let root=$('profiles');root.innerHTML='';s.profiles.forEach(p=>{let d=document.createElement('details');d.className='profile-card';d.dataset.profile=p.slot;d.open=(p.slot===openProfileSlot);d.addEventListener('toggle',()=>{if(d.open){openProfileSlot=p.slot;root.querySelectorAll('details.profile-card').forEach(other=>{if(other!==d)other.open=false;});}else if(openProfileSlot===p.slot)openProfileSlot=-1;});let sum=document.createElement('summary');let title=document.createElement('strong');title.textContent=p.name;sum.append(title);if(s.activeProfile===p.slot){let active=document.createElement('span');active.className='profile-active';active.textContent='目前使用';active.style.cssText='font-size:.78rem;color:#86efac;background:#14532d;padding:3px 7px;border-radius:12px';sum.append(active);}let meta=document.createElement('span');meta.className='profile-meta';meta.textContent=p.button+' · '+p.minMs/1000+'–'+p.maxMs/1000+' 秒 · '+p.holdMs+' ms';sum.append(meta);d.append(sum);
let body=document.createElement('div');body.className='profile-body';body.innerHTML='<label>Profile 名稱</label><input class="p-name" maxlength="30"><label>滑鼠按鍵</label><select class="p-button"><option value="back">Back</option><option value="forward">Forward</option><option value="middle">Middle</option></select><label>最短滑動間隔（秒）</label><input class="p-min" type="number" min="1" max="3600" step="1"><label>最長滑動間隔（秒）</label><input class="p-max" type="number" min="1" max="3600" step="1"><label>按鍵持續時間（毫秒）</label><input class="p-hold" type="number" min="10" max="500" step="1"><p class="dirty" hidden>● 有尚未保存的修改</p><div class="actions"><button type="button" class="save-profile">保存修改</button><button type="button" class="secondary apply-profile">套用</button><button type="button" class="secondary copy-current">以目前設定覆蓋此 Profile</button><button type="button" class="danger reset-profile">重設此 Profile</button></div>';
body.querySelector('.p-name').value=p.name;body.querySelector('.p-button').value=p.button;body.querySelector('.p-min').value=p.minMs/1000;body.querySelector('.p-max').value=p.maxMs/1000;body.querySelector('.p-hold').value=p.holdMs;
body.querySelectorAll('input,select').forEach(el=>{el.addEventListener('input',()=>profileDirtyMark(p.slot,true));el.addEventListener('change',()=>profileDirtyMark(p.slot,true));});body.querySelector('.save-profile').onclick=()=>saveProfile(p.slot);body.querySelector('.apply-profile').onclick=()=>profileApply(p.slot);body.querySelector('.copy-current').onclick=()=>copyCurrentToProfile(p.slot);body.querySelector('.reset-profile').onclick=()=>resetProfile(p.slot);d.append(body);root.append(d);});}
function renderShortcuts(s){let host=location.hostname||s.ip;$('shortcutBase').textContent='目前裝置：'+host+'（mDNS：autoswipe.local）';let root=$('shortcutLinks');root.innerHTML='';[['狀態','/status'],['暫停','/pause'],['繼續','/resume'],['切換暫停','/toggle']].forEach(([label,path])=>{let row=document.createElement('p');let url=document.createElement('code');url.textContent='http://'+host+path;let b=document.createElement('button');b.textContent='複製 URL';b.onclick=()=>copyText(url.textContent);row.append(document.createTextNode(label+'：'),url,b);root.append(row)});}
async function copyText(t){const textarea=document.createElement('textarea');textarea.value=t;textarea.readOnly=true;textarea.style.position='fixed';textarea.style.left='0';textarea.style.top='0';textarea.style.opacity='0';document.body.appendChild(textarea);textarea.focus();textarea.select();textarea.setSelectionRange(0,t.length);let success=false;try{success=document.execCommand('copy');}catch(e){success=false;}textarea.remove();if(success){msg('已複製！');}else{msg('複製失敗，請手動選取文字');}}
async function revealApiKey(){try{if(!firmwareKey){let j=await req('/api/apikey','');firmwareKey=j.key;}let field=$('firmwareApiKey');if(field.type==='text'){field.type='password';msg('API Key 已隱藏');}else{field.type='text';field.value=firmwareKey;msg('已從目前韌體讀取 API Key');}}catch(e){msg(e.message)}}
function clearFirmwareKey(){firmwareKey='';$('firmwareApiKey').value='';$('firmwareApiKey').type='password';}
async function copyFirmwareApiKey(){if(!firmwareKey){msg('請先點擊「查看」讀取 API Key');return;}await copyText(firmwareKey)}
$('loginForm').addEventListener('submit',async e=>{e.preventDefault();try{await req('/api/auth','key='+enc($('apiKey').value)+'&device='+enc(browserName()));$('apiKey').value='';msg('授權成功');await refresh();}catch(err){msg(err.message);}});
$('settingsForm').querySelectorAll('input,select').forEach(el=>{el.addEventListener('input',()=>currentDirty=true);el.addEventListener('change',()=>currentDirty=true);});
async function applyTemporary(){let v=values();if(!validate(v)){msg('設定無效：間隔需為 1–3600 秒且最長不小於最短；按住時間需為 10–500 ms');return;}let old=currentValues();try{await req('/api/settings',formBody(v));currentDirty=false;$('changeSummary').textContent=summary(old,v)+'（僅本次運作）';msg('已臨時套用；不會覆寫任何已保存設定');await refresh();}catch(e){msg(e.message)}}
async function saveCustomSettings(){let v=values();if(!validate(v)){msg('設定無效');return;}let old=currentValues();try{await req('/api/settings',formBody(v,'persist=1'));currentDirty=false;$('changeSummary').textContent=summary(old,v)+'（已保存為自訂設定）';msg('目前設定已保存；不會修改任何 Profile');await refresh();}catch(e){msg(e.message)}}
async function saveProfile(slot){let v=profileValues(slot);if(!validate(v)){msg('Profile 設定無效：請檢查間隔與按住時間');return;}let p=latest.profiles[slot],old={button:p.button,minSec:p.minMs/1000,maxSec:p.maxMs/1000,holdMs:p.holdMs};try{await req('/api/profiles/save','slot='+slot+'&name='+enc(v.name)+'&'+formBody(v));profileDirtyMark(slot,false);msg('已保存 '+v.name);$('changeSummary').textContent=summary(old,v)+'（保存至 '+v.name+'；尚未套用）';profileSignature='';await refresh();}catch(e){msg(e.message)}}
async function profileApply(slot){if(profileDirty.size){msg('有 Profile 尚未保存的修改，請先保存或重設後再套用。');return;}if(currentDirty&&!confirm('目前設定表單有尚未套用的修改；套用 Profile 將放棄這些輸入值。要繼續嗎？'))return;let old=currentValues(),p=latest.profiles[slot];try{await req('/api/profiles/apply','slot='+slot);currentDirty=false;$('changeSummary').textContent=summary(old,{button:p.button,minSec:p.minMs/1000,maxSec:p.maxMs/1000,holdMs:p.holdMs})+'（已套用 '+p.name+'）';msg('已套用 Profile：'+p.name);await refresh();}catch(e){msg(e.message)}}
function copyCurrentToProfile(slot){let v=values(),root=document.querySelector('[data-profile="'+slot+'"]');root.querySelector('.p-button').value=v.button;root.querySelector('.p-min').value=v.minSec;root.querySelector('.p-max').value=v.maxSec;root.querySelector('.p-hold').value=v.holdMs;profileDirtyMark(slot,true);msg('已將目前設定填入此 Profile，請按「保存修改」確認寫入');}
async function resetProfile(slot){if(profileDirty.size&&!profileDirty.has(slot)){msg('另一組 Profile 有尚未保存的修改，請先處理後再重設。');return;}if(!confirm('確定將此 Profile 恢復預設值？'))return;try{await req('/api/profiles/reset','slot='+slot);profileDirty.delete(slot);profileSignature='';await refresh();msg('Profile 已重設');}catch(e){msg(e.message)}}
async function resetAllProfiles(){if(!confirm('確定重設全部五組 Profiles？目前正在運作的設定不會因此改變。'))return;try{await req('/api/profiles/reset-all','');profileDirty.clear();profileSignature='';await refresh();msg('五組 Profiles 已全部重設');}catch(e){msg(e.message)}}
async function resetCurrent(){if(!confirm('確定將目前設定恢復預設值？Profiles、Wi-Fi、BLE 配對與瀏覽器授權不會被清除。'))return;let old=currentValues();try{await req('/api/reset','');currentDirty=false;let v={button:'back',minSec:25,maxSec:30,holdMs:50};$('changeSummary').textContent=summary(old,v);msg('目前設定已恢復預設值');await refresh()}catch(e){msg(e.message)}}
async function setBind(on){try{await req('/api/bind','enabled='+(on?'1':'0'));msg(on?'Bind Mode 已開啟':'Bind Mode 已關閉');await refresh()}catch(e){msg(e.message)}}
async function action(a){try{await req('/api/'+a,'');msg('已執行：'+a);await refresh()}catch(e){msg(e.message)}}
async function revoke(slot){try{await req('/api/revoke','slot='+slot);msg('已撤銷該瀏覽器');await refresh()}catch(e){msg(e.message)}}
async function logout(){try{await req('/api/logout','');clearFirmwareKey();msg('此瀏覽器授權已撤銷');await refresh()}catch(e){msg(e.message)}}
async function revokeAll(){if(!confirm('確定撤銷所有已信任的瀏覽器？'))return;try{await req('/api/revoke-all','');clearFirmwareKey();msg('所有信任裝置已撤銷');await refresh()}catch(e){msg(e.message)}}
refresh();setInterval(refresh,5000);
</script></body></html>
)HTML";

void handleDashboard() {
    server.send_P(200, "text/html; charset=utf-8", DASHBOARD_HTML);
}

void handleStatus() {
    if (!checkApiKey()) return;

    String json = "{";
    json += "\"paired\":";
    json += mouse.isPaired() ? "true" : "false";

    json += ",\"paused\":";
    json += paused ? "true" : "false";

    json += ",\"bindMode\":";
    json += bindMode ? "true" : "false";

    json += ",\"button\":\"";
    json += BUTTON_NAMES[cfg.button];
    json += "\"";

    json += ",\"holdMs\":";
    json += String(cfg.holdMs);

    json += ",\"minMs\":";
    json += String(cfg.minMs);

    json += ",\"maxMs\":";
    json += String(cfg.maxMs);

    json += ",\"ip\":\"";
    json += WiFi.localIP().toString();
    json += "\"";

    json += "}";

    server.send(200, "application/json", json);
}

void handlePause() {
    if (!checkApiKey()) return;

    setPaused(true);

    server.send(200, "application/json",
                "{\"success\":true,\"paused\":true}");
}

void handleResume() {
    if (!checkApiKey()) return;

    setPaused(false);

    server.send(200, "application/json",
                "{\"success\":true,\"paused\":false}");
}

void handleToggle() {
    if (!checkApiKey()) return;

    setPaused(!paused);

    String json = "{\"success\":true,\"paused\":";
    json += paused ? "true}" : "false}";

    server.send(200, "application/json", json);
}

// ---------- Wi-Fi ----------

void startHTTPServer() {
    if (WiFi.status() != WL_CONNECTED || httpStarted) {
        return;
    }

    server.begin();
    httpStarted = true;

    if (MDNS.begin("autoswipe")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("控制面板：http://autoswipe.local/");
    } else {
        Serial.println("mDNS 啟動失敗，請使用 IP 地址連線");
    }
    Serial.println("HTTP API 已啟動");
    Serial.print("IP 地址：http://");
    Serial.println(WiFi.localIP());
}

void setupHTTP() {
    server.collectHeaders(requestHeaders, 2);

    server.on("/", HTTP_GET, handleDashboard);

    // 新版瀏覽器控制介面：API Key 首次授權，之後使用持久化的 HttpOnly Cookie。
    server.on("/api/auth", HTTP_POST, handleWebLogin);
    server.on("/api/logout", HTTP_POST, handleWebLogout);
    server.on("/api/status", HTTP_GET, handleWebStatus);
    server.on("/api/apikey", HTTP_POST, handleWebApiKey);
    server.on("/api/pause", HTTP_POST, handleWebPause);
    server.on("/api/resume", HTTP_POST, handleWebResume);
    server.on("/api/toggle", HTTP_POST, handleWebToggle);
    server.on("/api/swipe", HTTP_POST, handleWebSwipe);
    server.on("/api/settings", HTTP_POST, handleWebSettings);
    server.on("/api/bind", HTTP_POST, handleWebBind);
    server.on("/api/reset", HTTP_POST, handleWebReset);
    server.on("/api/profiles/apply", HTTP_POST, handleProfileApply);
    server.on("/api/profiles/save", HTTP_POST, handleProfileSave);
    server.on("/api/profiles/rename", HTTP_POST, handleProfileRename);
    server.on("/api/profiles/reset", HTTP_POST, handleProfileReset);
    server.on("/api/profiles/reset-all", HTTP_POST, handleProfilesResetAll);
    server.on("/api/revoke", HTTP_POST, handleRevokeDevice);
    server.on("/api/revoke-all", HTTP_POST, handleRevokeAll);

    // 舊版 API 保留，避免影響既有 iOS 捷徑或外部整合。
    server.on("/status", HTTP_GET, handleStatus);
    server.on("/pause", HTTP_GET, handlePause);
    server.on("/resume", HTTP_GET, handleResume);
    server.on("/toggle", HTTP_GET, handleToggle);

    server.onNotFound([]() {
        server.send(404, "application/json",
                    "{\"error\":\"Not found\"}");
    });

    wifiManager.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);
    wifiManager.setConnectTimeout(15);

    Serial.println("正在連接已儲存的 Wi-Fi...");

    bool connected = wifiManager.autoConnect(
        CONFIG_AP_NAME,
        CONFIG_AP_PASSWORD
    );

    if (connected && WiFi.status() == WL_CONNECTED) {
        Serial.println("Wi-Fi 已連接！");
        startHTTPServer();
    } else {
        Serial.println("Wi-Fi 尚未連接，BLE 仍可正常使用");
    }
}

void enterWiFiConfigPortal() {
    Serial.println("\n========== Wi-Fi 重新配網 ==========");

    // 配網期間禁止自動滑動
    paused = true;
    bindMode = false;

    Serial.println("已暫停 AutoSwipe");

    // 停止原本使用 80 埠的 HTTP Server
    if (httpStarted) {
        server.stop();
        MDNS.end();
        httpStarted = false;
    }

    wifiManager.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);

    Serial.println("啟動 Wi-Fi 設定熱點...");
    Serial.print("熱點名稱：");
    Serial.println(CONFIG_AP_NAME);
    Serial.println("請使用 iPhone 連接並設定 Wi-Fi");

    bool connected = wifiManager.startConfigPortal(
        CONFIG_AP_NAME,
        CONFIG_AP_PASSWORD
    );

    // 配網結束，關閉 AP 模式
    WiFi.mode(WIFI_STA);

    if (connected && WiFi.status() == WL_CONNECTED) {
        Serial.println("Wi-Fi 設定成功！");
        Serial.print("IP 地址：");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("配網結束，但未成功連接 Wi-Fi");
    }

    // 如果仍有 Wi-Fi 連線，重新啟動 HTTP API
    startHTTPServer();

    Serial.println("AutoSwipe 目前維持暫停");
    Serial.println("========== 配網結束 ==========\n");
}

void maintainWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        startHTTPServer();

        if (httpStarted) {
            server.handleClient();
        }

        return;
    }

    if (httpStarted) {
        server.stop();
        MDNS.end();
        httpStarted = false;
        Serial.println("Wi-Fi 已斷線，HTTP API 暫停");
    }

    if (millis() - lastWifiAttempt >= 15000) {
        lastWifiAttempt = millis();

        Serial.println("Wi-Fi 斷線，嘗試重新連接...");
        WiFi.reconnect();
    }
}

// ---------- HTTP API END ----------

void scheduleNextSwipe() {
    uint32_t wait = random(cfg.minMs, cfg.maxMs + 1);
    nextSwipeAt = millis() + wait;
    Serial.printf("下次滑動：%.1f 秒後\n", wait / 1000.0);
}

void setPaused(bool p) {
    paused = p;
    Serial.println(p ? ">> 已暫停" : ">> 已繼續");
    if (!p) scheduleNextSwipe();
}

void setBindMode(bool on) {
    bindMode = on;
    if (on) {
        Serial.println(">> 綁定模式 ON（每 2 秒送一次按鈕，綁定完成後輸入 bind off）");
    } else {
        Serial.println(">> 綁定模式 OFF");
        scheduleNextSwipe();
    }
}

void triggerSwipe() {
    if (!mouse.isPaired()) {
        Serial.println("尚未配對，無法送出");
        return;
    }
    Serial.println("Swipe!");
    mouse.click(currentButton(), cfg.holdMs);
}

// ---------- 說明與狀態 ----------
void printHelp() {
    Serial.println(
        "\n========== 指令說明 ==========\n"
        "help / h / ?         顯示本說明\n"
        "status / s           顯示目前狀態與設定\n"
        "\n[控制]\n"
        "p                    暫停 / 繼續（切換）\n"
        "pause / resume       明確暫停 / 繼續\n"
        "swipe / b            立刻手動送出一次滑動\n"
        "bind / k [on|off]    綁定模式（讓 iOS 辨識按鈕）\n"
        "\n[設定，會自動儲存]\n"
        "interval <min> <max> 滑動間隔範圍（秒），例：interval 25 45\n"
        "button <name>        back / forward / middle\n"
        "hold <ms>            按鈕按住時間（10~500）\n"
        "reset                恢復預設值\n"
        "auth reset           撤銷所有瀏覽器授權\n"
        "\nBOOT 鍵：短按暫停 / 繼續，長按 3 秒進入 Wi-Fi 配網\n"
        "==============================");
}

void printStatus() {
    Serial.println("\n---------- 目前狀態 ----------");
    Serial.printf("連線：%s\n", mouse.isPaired() ? "已配對" : "未配對");
    Serial.printf("模式：%s\n", bindMode ? "綁定模式" : (paused ? "暫停中" : "運作中"));
    Serial.printf("間隔：%.1f ~ %.1f 秒\n", cfg.minMs / 1000.0, cfg.maxMs / 1000.0);
    Serial.printf("按鈕：%s（按住 %u ms）\n", BUTTON_NAMES[cfg.button], cfg.holdMs);
    if (mouse.isPaired() && !paused && !bindMode) {
        int32_t left = (int32_t)(nextSwipeAt - millis());
        if (left > 0) Serial.printf("下次滑動：%.1f 秒後\n", left / 1000.0);
    }
    Serial.println("------------------------------");
}

// ---------- 指令解析 ----------
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
        if (!a1 || !a2) { Serial.println("用法：interval <min秒> <max秒>"); return; }
        float mn = atof(a1), mx = atof(a2);
        if (mn < 1 || mx < mn) { Serial.println("錯誤：需要 min >= 1 且 max >= min"); return; }
        cfg.minMs = (uint32_t)(mn * 1000);
        cfg.maxMs = (uint32_t)(mx * 1000);
        saveConfig();
        saveActiveProfile(-1);
        Serial.printf("間隔已設為 %.1f ~ %.1f 秒\n", mn, mx);
        if (!paused && !bindMode) scheduleNextSwipe();

    } else if (is(cmd, "button")) {
        if (!a1) { Serial.println("用法：button <back|forward|middle>"); return; }
        int found = -1;
        for (int i = 0; i < 3; i++) if (strcasecmp(a1, BUTTON_NAMES[i]) == 0) found = i;
        if (found < 0) { Serial.println("錯誤：只能是 back / forward / middle"); return; }
        cfg.button = found;
        saveConfig();
        saveActiveProfile(-1);
        Serial.printf("按鈕已設為 %s（需在 iOS 重新綁定）\n", BUTTON_NAMES[found]);

    } else if (is(cmd, "hold")) {
        if (!a1) { Serial.println("用法：hold <ms>"); return; }
        int v = atoi(a1);
        if (v < 10 || v > 500) { Serial.println("錯誤：範圍 10~500"); return; }
        cfg.holdMs = v;
        saveConfig();
        saveActiveProfile(-1);
        Serial.printf("按住時間已設為 %d ms\n", v);

    } else if (is(cmd, "auth")) {
        if (a1 && strcasecmp(a1, "reset") == 0) {
            Preferences auth;
            auth.begin("swipe-auth", false);
            for (uint8_t i = 0; i < MAX_TRUSTED_DEVICES; ++i) auth.remove((String("s") + i).c_str());
            auth.end();
            Serial.println("已撤銷所有瀏覽器信任裝置");
        } else {
            Serial.println("用法：auth reset（清除所有瀏覽器授權）");
        }

    } else if (is(cmd, "reset")) {
        cfg = Config();
        saveConfig();
        saveActiveProfile(-1);
        if (!paused && !bindMode && mouse.isPaired()) scheduleNextSwipe();
        Serial.println("已恢復預設值");

    } else {
        Serial.printf("未知指令：%s（輸入 help 查看說明）\n", cmd);
    }
}

// ---------- 輸入處理 ----------
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

    // 序列埠監控視窗設成「沒有行結尾」時，停頓 100ms 也視為輸入完成
    if (len && millis() - lastRx > 100) {
        buf[len] = 0;
        len = 0;
        handleCommand(buf);
    }
}

void checkButton() {
    static bool lastReading = HIGH;
    static bool stableState = HIGH;

    static uint32_t lastDebounceTime = 0;
    static uint32_t pressStarted = 0;

    static bool longPressHandled = false;

    bool reading = digitalRead(PAUSE_BUTTON_PIN);

    // Debounce
    if (reading != lastReading) {
        lastDebounceTime = millis();
        lastReading = reading;
    }

    if (millis() - lastDebounceTime > 40 &&
        reading != stableState) {

        stableState = reading;

        if (stableState == LOW) {
            // 按下
            pressStarted = millis();
            longPressHandled = false;
        } else {
            // 放開
            if (!longPressHandled) {
                setPaused(!paused);
            }
        }
    }

    // 長按 3 秒進入 Wi-Fi 配網
    if (stableState == LOW &&
        !longPressHandled &&
        millis() - pressStarted >= 3000) {

        longPressHandled = true;
        enterWiFiConfigPortal();
    }
}

// ---------- 主程式 ----------
void setup() {
    Serial.begin(115200);
    pinMode(PAUSE_BUTTON_PIN, INPUT_PULLUP);
    randomSeed(esp_random());
    loadConfig();
    loadProfiles();

    mouse.setUpdateRate(HIDRate::Hz125);
    mouse.begin();

    setupHTTP();

    Serial.println("\nBLE Mouse 已啟動，等待 iPhone 配對...");
    Serial.println("輸入 help 查看所有指令");
}

void loop() {
    maintainWiFi();

    static bool lastPaired = false;

    readSerial();
    checkButton();

    bool paired = mouse.isPaired();
    if (paired != lastPaired) {
        lastPaired = paired;
        Serial.println(paired ? "iPhone 已配對！" : "iPhone 已斷線");
        if (paired) {
            readyAt = millis() + 1000;          // 非阻塞等待 1 秒
            scheduleNextSwipe();
        }
    }

    if (!paired || (int32_t)(millis() - readyAt) < 0) return;

    // 綁定模式：每 2 秒送一次按鈕，讓 iOS 辨識
    if (bindMode) {
        if (millis() - lastBindClick > 2000) {
            lastBindClick = millis();
            mouse.click(currentButton(), max<uint16_t>(cfg.holdMs, 80));
            Serial.println("已送出綁定按鈕");
        }
        return;
    }

    if (paused) return;

    if ((int32_t)(millis() - nextSwipeAt) >= 0) {
        triggerSwipe();
        scheduleNextSwipe();
    }
}