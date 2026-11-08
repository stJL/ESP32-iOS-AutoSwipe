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

// ===== Wi-Fi Settings =====
WiFiManager wifiManager;

const char* CONFIG_AP_NAME = "AutoSwipe-Setup";
const char* CONFIG_AP_PASSWORD = "SwipeSetup27";

const uint32_t CONFIG_PORTAL_TIMEOUT = 180;

// Please replace this with your own long random string before flashing
const char* API_KEY = "REPLACE_WITH_YOUR_OWN_RANDOM_API_KEY";  // Replace with your private random key before flashing; do not publish

WebServer server(80);

bool httpStarted = false;
uint32_t lastWifiAttempt = 0;

const char* requestHeaders[] = {"X-API-Key", "Cookie"};

// Browser trust settings: up to 4 devices, stored in a dedicated NVS namespace, retained across reboots.
const uint8_t MAX_TRUSTED_DEVICES = 4;
const char* SESSION_COOKIE = "AutoSwipeSession";
const uint32_t SESSION_COOKIE_MAX_AGE = 30UL * 24UL * 60UL * 60UL;

const int PAUSE_BUTTON_PIN = 0;     // BOOT button

// ===== Settings modifiable via Serial (auto-saved) =====
struct Config {
    uint32_t minMs  = 25000;        // Minimum interval
    uint32_t maxMs  = 30000;        // Maximum interval
    uint8_t  button = 0;            // 0=back 1=forward 2=middle
    uint16_t holdMs = 50;           // Button hold duration
};
Config cfg;

// Five profile presets stored separately in swipe-prof, without affecting existing swipe / swipe-auth data.
const uint8_t PROFILE_COUNT = 5;
struct Profile {
    String name;
    uint32_t minMs;
    uint32_t maxMs;
    uint8_t button;
    uint16_t holdMs;
};
Profile profiles[PROFILE_COUNT];
int8_t activeProfile = -1; // -1 means currently using custom settings
bool runtimeTemporary = false; // true = runtime temporary setting, restored upon reboot

const char* BUTTON_NAMES[] = {"back", "forward", "middle"};

void setPaused(bool p);
void triggerSwipe();
void scheduleNextSwipe();
void setBindMode(bool on);
void loadProfiles();
void saveProfile(uint8_t slot, const Profile& profile);
void saveActiveProfile(int8_t slot);

// ===== Runtime State =====
bool paused = false;
bool bindMode = false;
uint32_t nextSwipeAt = 0;
uint32_t readyAt = 0;
uint32_t lastBindClick = 0;

// ---------- Settings Access ----------
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
        // On first upgrade, migrate existing active settings into Profile 1 to prevent data loss.
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

// ---------- Actions ----------

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

// ---------- Browser Login and Persistent Trust ----------
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

// Return the firmware API Key only when explicitly requested by an authenticated browser session.
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

// Web settings: applied temporarily by default; persist=1 commits to primary settings storage.
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
        activeProfile = -1;       // Update runtime flag only, do not overwrite boot profile in NVS
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
    // If updating the active profile, existing running values are not silently overwritten;
    // UI marks them as temporary custom, while NVS preserves the profile for next boot.
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
    // Runtime values remain untouched; if a profile was previously active, show as custom state after reset.
    if (activeProfile >= 0) {
        activeProfile = -1;
        runtimeTemporary = true;
    }
    server.send(200, "application/json", "{\"success\":true}");
}

const char DASHBOARD_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>AutoSwipe</title>
<style>body{font:16px system-ui,sans-serif;max-width:620px;margin:30px auto;padding:0 16px;background:#111827;color:#e5e7eb}h1{font-size:1.7rem}section{background:#1f2937;padding:18px;border-radius:14px;margin:14px 0}input,button{font:inherit;padding:11px;border-radius:8px;border:0}input{width:95%;margin:8px 0;background:#374151;color:white;box-sizing:border-box}button{margin:5px 4px 5px 0;cursor:pointer;background:#2563eb;color:white}label{display:block;margin:12px 0 4px;font-weight:600}select{font:inherit;padding:11px;border-radius:8px;border:0;background:#374151;color:white;width:100%;margin:4px 0}button.secondary{background:#4b5563}button.danger{background:#b91c1c}.muted{color:#9ca3af;font-size:.9rem}#message{position:fixed;right:20px;bottom:20px;z-index:9999;max-width:min(90vw,360px);padding:10px 16px;margin:0;background:#374151;color:#fbbf24;border:1px solid #4b5563;border-radius:10px;box-shadow:0 4px 16px rgba(0,0,0,.35);opacity:0;transform:translateY(8px);pointer-events:none;transition:opacity .2s ease,transform .2s ease}#message.show{opacity:1;transform:translateY(0)}</style></head><body>
<h1>AutoSwipe Control Panel</h1><p class="muted">Used within the same Wi-Fi network. API Key is required for first-time login; the browser will be remembered afterwards.</p><p id="message"></p>
<section id="login"><h2>Device Authorization</h2><form id="loginForm"><input id="apiKey" type="password" placeholder="API Key" autocomplete="current-password" required><button type="submit">Log In & Trust This Device</button></form><p class="muted">API Key is only used for initial authorization and is never stored in the browser.</p></section>
<div id="panel" hidden>
<section><h2>Controls</h2><p id="state">Loading…</p><div class="actions"><button onclick="action('pause')">Pause</button><button onclick="action('resume')">Resume</button><button onclick="action('toggle')">Toggle</button><button onclick="action('swipe')">Swipe Now</button></div><div class="actions"><button class="secondary" onclick="setBind(true)">Enable Bind Mode</button><button class="secondary" onclick="setBind(false)">Disable Bind Mode</button></div><p class="muted">Bind Mode sends the selected mouse button every 2 seconds for iOS AssistiveTouch assignment.</p></section>
<section><h2>Current Settings</h2><p id="settings">—</p><p id="activeProfile" class="muted">—</p><p id="connection" class="muted"></p>
<h3>Edit Current Settings</h3><form id="settingsForm">
<label for="buttonSetting">Mouse Button</label><select id="buttonSetting"><option value="back">Back</option><option value="forward">Forward</option><option value="middle">Middle</option></select>
<label for="minSetting">Min Interval (sec)</label><input id="minSetting" type="number" min="1" max="3600" step="1" required>
<label for="maxSetting">Max Interval (sec)</label><input id="maxSetting" type="number" min="1" max="3600" step="1" required>
<label for="holdSetting">Hold Duration (ms)</label><input id="holdSetting" type="number" min="10" max="500" step="1" required>
<div class="actions"><button type="button" onclick="applyTemporary()">Apply Temporarily</button><button type="button" class="secondary" onclick="saveCustomSettings()">Save as Custom Settings</button></div>
</form><p id="changeSummary" class="summary"></p><p class="muted">Temporary settings affect the current session only and revert upon reboot. "Save as Custom Settings" stores values without modifying any profile.</p>
<div class="actions"><button class="danger" onclick="resetCurrent()">Reset Settings (Restore Defaults)</button></div></section>
<section><h2>Profiles</h2><p class="muted">Each profile can be edited individually. Saving updates the profile without immediate runtime application; "Apply" activates settings and persists across reboots.</p><div id="profiles"></div><button class="danger" onclick="resetAllProfiles()">Reset All Profiles</button></section>
<section><h2>iPhone Shortcuts Integration</h2><p class="muted">Uses legacy API endpoints (GET + X-API-Key), ideal for iOS "Get Contents of URL".</p><p id="shortcutBase"></p><label>Firmware API Key</label><div class="actions"><input id="firmwareApiKey" type="password" autocomplete="off" placeholder="Not revealed" readonly><button type="button" class="secondary" onclick="revealApiKey()">Reveal</button><button type="button" class="secondary" onclick="copyFirmwareApiKey()">Copy</button></div><p class="muted">Clicking "Reveal" fetches the key from ESP32. The key stays briefly in browser memory; HTTP is unencrypted, use only on trusted networks. Accessible to authenticated sessions.</p><div id="shortcutLinks"></div><p class="muted">Shortcut setup: Add "Get Contents of URL" action → Set Method to GET → Expand Headers → Add X-API-Key with your API Key value. Use the URLs above. Do not put the API Key in URL query parameters.</p></section>
<section><h2>Trusted Browsers</h2><div id="devices"></div><button class="danger" onclick="logout()">Revoke This Browser</button><button class="danger" onclick="revokeAll()">Revoke All Trusted Devices</button></section>
</div>
<script>
const $=id=>document.getElementById(id);
let msgTimer=null;
function msg(t){const el=$('message');el.textContent=t;el.classList.add('show');clearTimeout(msgTimer);msgTimer=setTimeout(()=>el.classList.remove('show'),2200);}
let latest=null;
let currentDirty=false,profileSignature='',openProfileSlot=-1,profileDirty=new Set(),firmwareKey='';
async function req(path,body){let post=body!==undefined;let r=await fetch(path,{method:post?'POST':'GET',headers:post?{'Content-Type':'application/x-www-form-urlencoded'}:{},body:post?(body||''):undefined,credentials:'same-origin',cache:'no-store'});let j={};try{j=await r.json()}catch(e){}if(!r.ok)throw Error(j.error||('HTTP '+r.status));return j;}
function enc(v){return encodeURIComponent(v)}
function browserName(){let u=navigator.userAgent;if(/iPhone|iPad|iPod/.test(u))return 'iPhone/iPad Safari';if(/Android/.test(u))return 'Android Browser';if(/Firefox/.test(u))return 'Firefox';if(/Edg/.test(u))return 'Edge';if(/Chrome/.test(u))return 'Chrome';if(/Safari/.test(u))return 'Safari';return 'Browser';}
function values(){return {button:$('buttonSetting').value,minSec:Number($('minSetting').value),maxSec:Number($('maxSetting').value),holdMs:Number($('holdSetting').value)}}
function validate(v){return Number.isInteger(v.minSec)&&Number.isInteger(v.maxSec)&&Number.isInteger(v.holdMs)&&v.minSec>=1&&v.maxSec>=v.minSec&&v.maxSec<=3600&&v.holdMs>=10&&v.holdMs<=500}
function formBody(v,extra=''){return 'button='+enc(v.button)+'&minSec='+v.minSec+'&maxSec='+v.maxSec+'&holdMs='+v.holdMs+(extra?'&'+extra:'')}
function currentValues(){return latest?{button:latest.button,minSec:latest.minMs/1000,maxSec:latest.maxMs/1000,holdMs:latest.holdMs}:null}
function summary(oldV,newV){let names={back:'Back',forward:'Forward',middle:'Middle'},a=[];if(oldV.button!==newV.button)a.push('Button: '+names[oldV.button]+' → '+names[newV.button]);if(oldV.minSec!==newV.minSec||oldV.maxSec!==newV.maxSec)a.push('Interval: '+oldV.minSec+'–'+oldV.maxSec+' s → '+newV.minSec+'–'+newV.maxSec+' s');if(oldV.holdMs!==newV.holdMs)a.push('Hold: '+oldV.holdMs+' ms → '+newV.holdMs+' ms');return a.length?a.join('; '):'No changes'}
function profileValues(slot){let root=document.querySelector('[data-profile="'+slot+'"]');return {name:root.querySelector('.p-name').value,button:root.querySelector('.p-button').value,minSec:Number(root.querySelector('.p-min').value),maxSec:Number(root.querySelector('.p-max').value),holdMs:Number(root.querySelector('.p-hold').value)}}
function profileDirtyMark(slot,on){if(on)profileDirty.add(slot);else profileDirty.delete(slot);let root=document.querySelector('[data-profile="'+slot+'"]');if(root){let badge=root.querySelector('.dirty');if(badge)badge.hidden=!on;}}
async function refresh(){try{let s=await req('/api/status');latest=s;$('login').hidden=true;$('panel').hidden=false;$('state').textContent='BLE: '+(s.paired?'Paired':'Not paired')+'; State: '+(s.paused?'Paused':'Active')+(s.bindMode?'; Bind Mode':'');$('settings').textContent='Button: '+s.button+'; Hold: '+s.holdMs+' ms; Interval: '+(s.minMs/1000)+'–'+(s.maxMs/1000)+' s';$('activeProfile').textContent='Current Profile: '+(s.temporary?'Temporary Custom (reverts to saved settings on reboot)':(s.activeProfile>=0?s.profiles[s.activeProfile].name+' (Saved)':'Custom Settings (Saved)'));$('connection').textContent='IP: '+s.ip+'; URL: http://autoswipe.local/';
if(!currentDirty&&!['buttonSetting','minSetting','maxSetting','holdSetting'].includes(document.activeElement.id)){$('buttonSetting').value=s.button;$('minSetting').value=s.minMs/1000;$('maxSetting').value=s.maxMs/1000;$('holdSetting').value=s.holdMs;}
let sig=JSON.stringify({profiles:s.profiles,activeProfile:s.activeProfile});if(sig!==profileSignature&&!profileDirty.size){profileSignature=sig;renderProfiles(s);}
renderShortcuts(s);$('devices').innerHTML='';s.devices.forEach(d=>{let row=document.createElement('div');row.className='device';let label=document.createElement('span');label.textContent=d.name+(d.current?' (Current Browser)':'');let b=document.createElement('button');b.className='danger';b.textContent='Revoke';b.onclick=()=>revoke(d.slot);row.append(label,b);$('devices').append(row);});}catch(e){clearFirmwareKey();$('panel').hidden=true;$('login').hidden=false;if(e.message!=='HTTP 401')msg(e.message);}}
function renderProfiles(s){let root=$('profiles');root.innerHTML='';s.profiles.forEach(p=>{let d=document.createElement('details');d.className='profile-card';d.dataset.profile=p.slot;d.open=(p.slot===openProfileSlot);d.addEventListener('toggle',()=>{if(d.open){openProfileSlot=p.slot;root.querySelectorAll('details.profile-card').forEach(other=>{if(other!==d)other.open=false;});}else if(openProfileSlot===p.slot)openProfileSlot=-1;});let sum=document.createElement('summary');let title=document.createElement('strong');title.textContent=p.name;sum.append(title);if(s.activeProfile===p.slot){let active=document.createElement('span');active.className='profile-active';active.textContent='Active';active.style.cssText='font-size:.78rem;color:#86efac;background:#14532d;padding:3px 7px;border-radius:12px';sum.append(active);}let meta=document.createElement('span');meta.className='profile-meta';meta.textContent=p.button+' · '+p.minMs/1000+'–'+p.maxMs/1000+' s · '+p.holdMs+' ms';sum.append(meta);d.append(sum);
let body=document.createElement('div');body.className='profile-body';body.innerHTML='<label>Profile Name</label><input class="p-name" maxlength="30"><label>Mouse Button</label><select class="p-button"><option value="back">Back</option><option value="forward">Forward</option><option value="middle">Middle</option></select><label>Min Interval (sec)</label><input class="p-min" type="number" min="1" max="3600" step="1"><label>Max Interval (sec)</label><input class="p-max" type="number" min="1" max="3600" step="1"><label>Hold Duration (ms)</label><input class="p-hold" type="number" min="10" max="500" step="1"><p class="dirty" hidden>● Unsaved changes</p><div class="actions"><button type="button" class="save-profile">Save Changes</button><button type="button" class="secondary apply-profile">Apply</button><button type="button" class="secondary copy-current">Overwrite Profile with Current Settings</button><button type="button" class="danger reset-profile">Reset Profile</button></div>';
body.querySelector('.p-name').value=p.name;body.querySelector('.p-button').value=p.button;body.querySelector('.p-min').value=p.minMs/1000;body.querySelector('.p-max').value=p.maxMs/1000;body.querySelector('.p-hold').value=p.holdMs;
body.querySelectorAll('input,select').forEach(el=>{el.addEventListener('input',()=>profileDirtyMark(p.slot,true));el.addEventListener('change',()=>profileDirtyMark(p.slot,true));});body.querySelector('.save-profile').onclick=()=>saveProfile(p.slot);body.querySelector('.apply-profile').onclick=()=>profileApply(p.slot);body.querySelector('.copy-current').onclick=()=>copyCurrentToProfile(p.slot);body.querySelector('.reset-profile').onclick=()=>resetProfile(p.slot);d.append(body);root.append(d);});}
function renderShortcuts(s){let host=location.hostname||s.ip;$('shortcutBase').textContent='Current Device: '+host+' (mDNS: autoswipe.local)';let root=$('shortcutLinks');root.innerHTML='';[['Status','/status'],['Pause','/pause'],['Resume','/resume'],['Toggle','/toggle']].forEach(([label,path])=>{let row=document.createElement('p');let url=document.createElement('code');url.textContent='http://'+host+path;let b=document.createElement('button');b.textContent='Copy URL';b.onclick=()=>copyText(url.textContent);row.append(document.createTextNode(label+': '),url,b);root.append(row)});}
async function copyText(t){const textarea=document.createElement('textarea');textarea.value=t;textarea.readOnly=true;textarea.style.position='fixed';textarea.style.left='0';textarea.style.top='0';textarea.style.opacity='0';document.body.appendChild(textarea);textarea.focus();textarea.select();textarea.setSelectionRange(0,t.length);let success=false;try{success=document.execCommand('copy');}catch(e){success=false;}textarea.remove();if(success){msg('Copied!');}else{msg('Copy failed, please copy text manually');}}
async function revealApiKey(){try{if(!firmwareKey){let j=await req('/api/apikey','');firmwareKey=j.key;}let field=$('firmwareApiKey');if(field.type==='text'){field.type='password';msg('API Key hidden');}else{field.type='text';field.value=firmwareKey;msg('Retrieved API Key from firmware');}}catch(e){msg(e.message)}}
function clearFirmwareKey(){firmwareKey='';$('firmwareApiKey').value='';$('firmwareApiKey').type='password';}
async function copyFirmwareApiKey(){if(!firmwareKey){msg('Please click Reveal to load the API Key first');return;}await copyText(firmwareKey)}
$('loginForm').addEventListener('submit',async e=>{e.preventDefault();try{await req('/api/auth','key='+enc($('apiKey').value)+'&device='+enc(browserName()));$('apiKey').value='';msg('Authorization successful');await refresh();}catch(err){msg(err.message);}});
$('settingsForm').querySelectorAll('input,select').forEach(el=>{el.addEventListener('input',()=>currentDirty=true);el.addEventListener('change',()=>currentDirty=true);});
async function applyTemporary(){let v=values();if(!validate(v)){msg('Invalid settings: Interval must be 1–3600 sec (max >= min); hold duration 10–500 ms');return;}let old=currentValues();try{await req('/api/settings',formBody(v));currentDirty=false;$('changeSummary').textContent=summary(old,v)+' (Current session only)';msg('Applied temporarily; no saved settings were overwritten');await refresh();}catch(e){msg(e.message)}}
async function saveCustomSettings(){let v=values();if(!validate(v)){msg('Invalid settings');return;}let old=currentValues();try{await req('/api/settings',formBody(v,'persist=1'));currentDirty=false;$('changeSummary').textContent=summary(old,v)+' (Saved as custom settings)';msg('Current settings saved; profiles were not modified');await refresh();}catch(e){msg(e.message)}}
async function saveProfile(slot){let v=profileValues(slot);if(!validate(v)){msg('Invalid profile settings: check interval and hold duration');return;}let p=latest.profiles[slot],old={button:p.button,minSec:p.minMs/1000,maxSec:p.maxMs/1000,holdMs:p.holdMs};try{await req('/api/profiles/save','slot='+slot+'&name='+enc(v.name)+'&'+formBody(v));profileDirtyMark(slot,false);msg('Saved '+v.name);$('changeSummary').textContent=summary(old,v)+' (Saved to '+v.name+'; not yet applied)';profileSignature='';await refresh();}catch(e){msg(e.message)}}
async function profileApply(slot){if(profileDirty.size){msg('A profile has unsaved changes. Please save or reset before applying.');return;}if(currentDirty&&!confirm('Current settings form has unapplied changes; applying a profile will discard them. Continue?'))return;let old=currentValues(),p=latest.profiles[slot];try{await req('/api/profiles/apply','slot='+slot);currentDirty=false;$('changeSummary').textContent=summary(old,{button:p.button,minSec:p.minMs/1000,maxSec:p.maxMs/1000,holdMs:p.holdMs})+' (Applied '+p.name+')';msg('Applied Profile: '+p.name);await refresh();}catch(e){msg(e.message)}}
function copyCurrentToProfile(slot){let v=values(),root=document.querySelector('[data-profile="'+slot+'"]');root.querySelector('.p-button').value=v.button;root.querySelector('.p-min').value=v.minSec;root.querySelector('.p-max').value=v.maxSec;root.querySelector('.p-hold').value=v.holdMs;profileDirtyMark(slot,true);msg('Populated profile with current settings. Click Save Changes to commit.');}
async function resetProfile(slot){if(profileDirty.size&&!profileDirty.has(slot)){msg('Another profile has unsaved changes. Please resolve them first.');return;}if(!confirm('Reset this profile to default values?'))return;try{await req('/api/profiles/reset','slot='+slot);profileDirty.delete(slot);profileSignature='';await refresh();msg('Profile reset');}catch(e){msg(e.message)}}
async function resetAllProfiles(){if(!confirm('Reset all five profiles to defaults? Current running settings will not be changed.'))return;try{await req('/api/profiles/reset-all','');profileDirty.clear();profileSignature='';await refresh();msg('All five profiles have been reset');}catch(e){msg(e.message)}}
async function resetCurrent(){if(!confirm('Reset current settings to defaults? Profiles, Wi-Fi, BLE pairing, and browser trust will not be cleared.'))return;let old=currentValues();try{await req('/api/reset','');currentDirty=false;let v={button:'back',minSec:25,maxSec:30,holdMs:50};$('changeSummary').textContent=summary(old,v);msg('Current settings restored to defaults');await refresh()}catch(e){msg(e.message)}}
async function setBind(on){try{await req('/api/bind','enabled='+(on?'1':'0'));msg(on?'Bind Mode enabled':'Bind Mode disabled');await refresh()}catch(e){msg(e.message)}}
async function action(a){try{await req('/api/'+a,'');msg('Executed: '+a);await refresh()}catch(e){msg(e.message)}}
async function revoke(slot){try{await req('/api/revoke','slot='+slot);msg('Browser authorization revoked');await refresh()}catch(e){msg(e.message)}}
async function logout(){try{await req('/api/logout','');clearFirmwareKey();msg('This browser has been revoked');await refresh()}catch(e){msg(e.message)}}
async function revokeAll(){if(!confirm('Revoke all trusted browsers?'))return;try{await req('/api/revoke-all','');clearFirmwareKey();msg('All trusted devices revoked');await refresh()}catch(e){msg(e.message)}}
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
        Serial.println("Control panel: http://autoswipe.local/");
    } else {
        Serial.println("mDNS responder failed to start. Please connect via IP address.");
    }
    Serial.println("HTTP API started");
    Serial.print("IP Address: http://");
    Serial.println(WiFi.localIP());
}

void setupHTTP() {
    server.collectHeaders(requestHeaders, 2);

    server.on("/", HTTP_GET, handleDashboard);

    // Modern browser interface: API Key for first authorization, followed by persistent HttpOnly Cookie.
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

    // Legacy API endpoints preserved for iOS Shortcuts and external integrations.
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

    Serial.println("Connecting to saved Wi-Fi...");

    bool connected = wifiManager.autoConnect(
        CONFIG_AP_NAME,
        CONFIG_AP_PASSWORD
    );

    if (connected && WiFi.status() == WL_CONNECTED) {
        Serial.println("Wi-Fi connected!");
        startHTTPServer();
    } else {
        Serial.println("Wi-Fi not connected. BLE remains operational.");
    }
}

void enterWiFiConfigPortal() {
    Serial.println("\n========== Wi-Fi Configuration Portal ==========");

    // Inhibit auto swiping during Wi-Fi setup
    paused = true;
    bindMode = false;

    Serial.println("AutoSwipe paused");

    // Stop HTTP server running on port 80
    if (httpStarted) {
        server.stop();
        MDNS.end();
        httpStarted = false;
    }

    wifiManager.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT);

    Serial.println("Starting Wi-Fi setup Access Point...");
    Serial.print("AP Name: ");
    Serial.println(CONFIG_AP_NAME);
    Serial.println("Connect using your iPhone to configure Wi-Fi");

    bool connected = wifiManager.startConfigPortal(
        CONFIG_AP_NAME,
        CONFIG_AP_PASSWORD
    );

    // Portal finished, switch back to Station mode
    WiFi.mode(WIFI_STA);

    if (connected && WiFi.status() == WL_CONNECTED) {
        Serial.println("Wi-Fi configured successfully!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("Portal ended without Wi-Fi connection.");
    }

    // Restart HTTP server if connected
    startHTTPServer();

    Serial.println("AutoSwipe remains paused");
    Serial.println("========== Portal Closed ==========\n");
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
        Serial.println("Wi-Fi disconnected, HTTP API suspended");
    }

    if (millis() - lastWifiAttempt >= 15000) {
        lastWifiAttempt = millis();

        Serial.println("Wi-Fi disconnected, attempting reconnection...");
        WiFi.reconnect();
    }
}

// ---------- HTTP API END ----------

void scheduleNextSwipe() {
    uint32_t wait = random(cfg.minMs, cfg.maxMs + 1);
    nextSwipeAt = millis() + wait;
    Serial.printf("Next swipe in: %.1f sec\n", wait / 1000.0);
}

void setPaused(bool p) {
    paused = p;
    Serial.println(p ? ">> Paused" : ">> Resumed");
    if (!p) scheduleNextSwipe();
}

void setBindMode(bool on) {
    bindMode = on;
    if (on) {
        Serial.println(">> Bind mode ON (sends button click every 2s; type 'bind off' when done)");
    } else {
        Serial.println(">> Bind mode OFF");
        scheduleNextSwipe();
    }
}

void triggerSwipe() {
    if (!mouse.isPaired()) {
        Serial.println("Not paired, cannot send click");
        return;
    }
    Serial.println("Swipe!");
    mouse.click(currentButton(), cfg.holdMs);
}

// ---------- Help and Status ----------
void printHelp() {
    Serial.println(
        "\n========== Command Reference ==========\n"
        "help / h / ?         Show this help menu\n"
        "status / s           Show current status and settings\n"
        "\n[Control]\n"
        "p                    Pause / Resume (Toggle)\n"
        "pause / resume       Explicit pause / resume\n"
        "swipe / b            Trigger an immediate manual swipe\n"
        "bind / k [on|off]    Bind mode (helps iOS detect the button)\n"
        "\n[Settings (Auto-saved)]\n"
        "interval <min> <max> Set swipe interval range (seconds), e.g. interval 25 45\n"
        "button <name>        back / forward / middle\n"
        "hold <ms>            Button hold duration (10~500 ms)\n"
        "reset                Restore default settings\n"
        "auth reset           Revoke all browser authorizations\n"
        "\nBOOT button: Short press to pause/resume, hold for 3s to enter Wi-Fi config portal\n"
        "=======================================");
}

void printStatus() {
    Serial.println("\n---------- Current Status ----------");
    Serial.printf("Connection: %s\n", mouse.isPaired() ? "Paired" : "Not paired");
    Serial.printf("Mode: %s\n", bindMode ? "Bind Mode" : (paused ? "Paused" : "Active"));
    Serial.printf("Interval: %.1f ~ %.1f sec\n", cfg.minMs / 1000.0, cfg.maxMs / 1000.0);
    Serial.printf("Button: %s (Hold %u ms)\n", BUTTON_NAMES[cfg.button], cfg.holdMs);
    if (mouse.isPaired() && !paused && !bindMode) {
        int32_t left = (int32_t)(nextSwipeAt - millis());
        if (left > 0) Serial.printf("Next swipe in: %.1f sec\n", left / 1000.0);
    }
    Serial.println("------------------------------------");
}

// ---------- Command Parser ----------
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
        if (!a1 || !a2) { Serial.println("Usage: interval <minSec> <maxSec>"); return; }
        float mn = atof(a1), mx = atof(a2);
        if (mn < 1 || mx < mn) { Serial.println("Error: Requires min >= 1 and max >= min"); return; }
        cfg.minMs = (uint32_t)(mn * 1000);
        cfg.maxMs = (uint32_t)(mx * 1000);
        saveConfig();
        saveActiveProfile(-1);
        Serial.printf("Interval set to %.1f ~ %.1f sec\n", mn, mx);
        if (!paused && !bindMode) scheduleNextSwipe();

    } else if (is(cmd, "button")) {
        if (!a1) { Serial.println("Usage: button <back|forward|middle>"); return; }
        int found = -1;
        for (int i = 0; i < 3; i++) if (strcasecmp(a1, BUTTON_NAMES[i]) == 0) found = i;
        if (found < 0) { Serial.println("Error: Must be back / forward / middle"); return; }
        cfg.button = found;
        saveConfig();
        saveActiveProfile(-1);
        Serial.printf("Button set to %s (requires re-binding in iOS)\n", BUTTON_NAMES[found]);

    } else if (is(cmd, "hold")) {
        if (!a1) { Serial.println("Usage: hold <ms>"); return; }
        int v = atoi(a1);
        if (v < 10 || v > 500) { Serial.println("Error: Range must be 10~500"); return; }
        cfg.holdMs = v;
        saveConfig();
        saveActiveProfile(-1);
        Serial.printf("Hold duration set to %d ms\n", v);

    } else if (is(cmd, "auth")) {
        if (a1 && strcasecmp(a1, "reset") == 0) {
            Preferences auth;
            auth.begin("swipe-auth", false);
            for (uint8_t i = 0; i < MAX_TRUSTED_DEVICES; ++i) auth.remove((String("s") + i).c_str());
            auth.end();
            Serial.println("Revoked all trusted browser devices");
        } else {
            Serial.println("Usage: auth reset (clears all browser authorizations)");
        }

    } else if (is(cmd, "reset")) {
        cfg = Config();
        saveConfig();
        saveActiveProfile(-1);
        if (!paused && !bindMode && mouse.isPaired()) scheduleNextSwipe();
        Serial.println("Restored to default settings");

    } else {
        Serial.printf("Unknown command: %s (type 'help' for command reference)\n", cmd);
    }
}

// ---------- Input Handling ----------
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

    // If Serial Monitor is configured with "No line ending", treat a 100ms pause as end of line
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
            // Button pressed down
            pressStarted = millis();
            longPressHandled = false;
        } else {
            // Button released
            if (!longPressHandled) {
                setPaused(!paused);
            }
        }
    }

    // Long press for 3 seconds to enter Wi-Fi config portal
    if (stableState == LOW &&
        !longPressHandled &&
        millis() - pressStarted >= 3000) {

        longPressHandled = true;
        enterWiFiConfigPortal();
    }
}

// ---------- Main Program ----------
void setup() {
    Serial.begin(115200);
    pinMode(PAUSE_BUTTON_PIN, INPUT_PULLUP);
    randomSeed(esp_random());
    loadConfig();
    loadProfiles();

    mouse.setUpdateRate(HIDRate::Hz125);
    mouse.begin();

    setupHTTP();

    Serial.println("\nBLE Mouse started, waiting for iPhone to pair...");
    Serial.println("Type 'help' to view all commands");
}

void loop() {
    maintainWiFi();

    static bool lastPaired = false;

    readSerial();
    checkButton();

    bool paired = mouse.isPaired();
    if (paired != lastPaired) {
        lastPaired = paired;
        Serial.println(paired ? "iPhone paired!" : "iPhone disconnected");
        if (paired) {
            readyAt = millis() + 1000;          // Non-blocking 1s wait
            scheduleNextSwipe();
        }
    }

    if (!paired || (int32_t)(millis() - readyAt) < 0) return;

    // Bind mode: send button click every 2s for iOS detection
    if (bindMode) {
        if (millis() - lastBindClick > 2000) {
            lastBindClick = millis();
            mouse.click(currentButton(), max<uint16_t>(cfg.holdMs, 80));
            Serial.println("Bind button click sent");
        }
        return;
    }

    if (paused) return;

    if ((int32_t)(millis() - nextSwipeAt) >= 0) {
        triggerSwipe();
        scheduleNextSwipe();
    }
}