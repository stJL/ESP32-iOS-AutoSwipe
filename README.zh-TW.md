# ESP32 iOS AutoSwipe

使用 ESP32 模擬 Bluetooth LE HID 滑鼠，定時向 iPhone 發送滑鼠按鍵點擊，觸發 iOS AssistiveTouch 中預先設定的動作（例如捲動或滑動手勢），讓影片瀏覽等操作可以自動執行。

> **專案狀態：** DIY 實驗專案。iOS 版本不同，設定選單名稱與操作方式可能略有差異。本文以 V2 韌體的操作流程為準。

## 功能特色

- 透過 Bluetooth LE HID Mouse 連接 iPhone。
- 可設定自動觸發的最短／最長間隔（秒）。
- 可選擇 Back、Forward、Middle 三種滑鼠按鍵。
- 可設定按鍵按住時間（hold duration）。
- BOOT 實體按鍵可暫停／繼續，以及開啟 Wi-Fi 設定入口。
- 瀏覽器 Web Panel，可控制裝置及調整設定。
- 五組可獨立編輯、摺疊的 Profiles。
- 支援暫時套用設定或儲存自訂設定。
- 提供 HTTP API，可供 Apple Shortcuts 等區域網路自動化使用。
- Web Panel 使用 API Key 登入，並以可信任瀏覽器 Session 維持登入。
- 設定與 Profiles 儲存於 ESP32 NVS。

## 硬體與軟體需求

### 硬體

- ESP32-WROOM-32 開發板（傳統 ESP32，並非 ESP32-S3 專用版本）。
- 支援 Bluetooth 與 AssistiveTouch 的 iPhone。
- 可傳輸資料的 USB 線，用於燒錄與 Serial Monitor。

### 韌體開發環境

V2 專案目前使用：

- Arduino IDE
- Arduino-ESP32 Core 3.3.12
- NimBLE-Arduino 2.5.2
- HijelHID_BLEMouse library
- WiFiManager by tzapu

請使用與韌體相容的 Core 和函式庫版本；若版本不同，編譯結果或 BLE 行為可能有所差異。

## 安裝與燒錄

1. 使用 Arduino IDE 開啟 V2 `.ino` 程式。
2. 確認已安裝上方列出的必要函式庫。
3. **燒錄前請設定自己的 API Key。** 找到 `API_KEY` 宣告，將公開版佔位文字換成一組夠長、隨機產生的私人金鑰。
4. 連接 ESP32，選擇正確的 Board 與 Serial Port。
5. 在 **Tools → Partition Scheme** 選擇 **Minimal SPIFFS**（目前測試可讓韌體順利放入的配置）。
6. 編譯並 Upload。
7. 開啟 Serial Monitor，使用程式設定的鮑率，依照啟動訊息確認裝置狀態，並記下輸出的 IP 位址。

**公開在 GitHub 的程式碼請保留 API Key 佔位文字，不要放入可用的私人金鑰。** 如果公開原始碼含有真實金鑰，任何人都可能複製使用。

## 首次 Wi-Fi 設定

ESP32 開機後會先嘗試使用已儲存的 Wi-Fi 帳密連線。如果無法連線，便會啟動設定 AP：

- **SSID：** `AutoSwipe-Setup`
- **Password：** `SwipeSetup27`
- **設定頁面：** 連上設定 Wi-Fi 後，通常可開啟 `http://192.168.4.1/`

在設定頁面選擇 Wi-Fi 網路並輸入密碼，儲存後 ESP32 會嘗試連線。

日後若要重新設定 Wi-Fi，長按 ESP32 的 **BOOT** 按鍵約 3 秒。裝置進入設定流程時會暫停自動觸發。設定入口有逾時機制，若入口關閉，請重新長按 BOOT。

> 設定 AP 的密碼是韌體預設值，不能取代網路安全措施。請使用可信任的 Wi-Fi，並避免不必要地讓設定 AP 長時間開啟。

## 與 iPhone 配對

1. 開啟 ESP32，等待 BLE HID Mouse 開始廣播。
2. 在 iPhone 開啟 **設定 → 藍牙**。
3. 找到 AutoSwipe 的滑鼠裝置並配對／連線。
4. 開啟 **設定 → 輔助使用 → 觸控 → AssistiveTouch**，啟用 AssistiveTouch。
5. 在 AssistiveTouch 的裝置／按鈕設定中，將已連線的藍牙滑鼠按鍵指定為希望執行的動作。
6. 開啟自動操作前，先手動測試該按鍵是否能正確觸發動作。

Apple 可能隨 iOS 版本調整選單名稱與位置。AutoSwipe 本身只負責送出滑鼠按鍵點擊；點擊實際執行什麼動作，取決於 iPhone 上的 AssistiveTouch 設定。

### 按鍵對照

| 韌體設定值 | 滑鼠按鍵 |
|---:|---|
| `0` | Back |
| `1` | Forward |
| `2` | Middle |

可在 Web Panel 或 Serial Monitor 選擇按鍵。若需要協助辨識／設定 iOS 上的按鍵，可使用 **Bind** 模式；裝置會定期送出目前選定的按鍵事件。

## 實體按鍵操作

| BOOT 操作 | 功能 |
|---|---|
| 短按 | 切換 Pause / Resume |
| 長按約 3 秒 | 開啟 Wi-Fi 設定入口 |

## Web Panel

ESP32 成功連上 Wi-Fi 後，可使用以下網址：

- `http://autoswipe.local/`（網路支援 mDNS 時），或
- `http://<ESP32-IP>/`，使用 Serial Monitor 或路由器 DHCP 清單顯示的 IP。

首次登入時輸入韌體內設定的 API Key。登入後，瀏覽器會取得 Session Cookie，之後同一瀏覽器不必每次操作都重新輸入金鑰。

### Current Settings（目前設定）

此區域控制目前正在執行的設定：

- Mouse Button：滑鼠按鍵
- Minimum Interval：最短觸發間隔
- Maximum Interval：最長觸發間隔
- Hold Duration：按鍵按住時間

暫時套用只會影響目前執行狀態，不會寫入永久設定。若希望重新開機後仍保留自訂值，請使用儲存／持久化設定的功能。

### Profiles（設定檔）

V2 提供五組 Profiles，每組可獨立儲存名稱與設定值。

- 點開某組 Profile 即可編輯；收合後可減少頁面佔用空間。
- **Save Changes**：只儲存該 Profile，不會立即套用。
- **Apply**：將該 Profile 套用到目前執行設定，並設為目前啟用的 Profile。
- 單獨 Reset：將指定 Profile 還原為預設值。
- Reset All：重設五組 Profile 定義，但不會改變當下正在執行的設定。

**Save 與 Apply 是兩個不同動作。** 你可以先編輯、儲存多組 Profile，而不影響目前正在執行的操作。

### Trusted Browsers（可信任瀏覽器）

Web Panel 最多支援四個可信任瀏覽器 Session。可以單獨撤銷某個瀏覽器，也可以撤銷全部瀏覽器。撤銷 Session 不會更改韌體內的 API Key。

## iOS Shortcuts／HTTP API

舊版相容 HTTP API 可供 Apple Shortcuts 使用。這些端點使用 **GET**，並且必須在 Header 傳入 `X-API-Key`。

基本網址：

```text
http://autoswipe.local
```

若 mDNS 無法使用，請將主機名稱換成 ESP32 的 IP，例如 `http://192.168.1.50`。

| Method | Path | 功能 |
|---|---|---|
| GET | `/status` | 讀取裝置狀態 |
| GET | `/pause` | 暫停自動觸發 |
| GET | `/resume` | 恢復自動觸發 |
| GET | `/toggle` | 切換暫停／繼續 |

### Apple Shortcuts 設定方式

1. 開啟「捷徑」App，新增動作 **Get Contents of URL（取得 URL 的內容）**。
2. URL 輸入，例如：

   ```text
   http://autoswipe.local/pause
   ```
3. 展開動作選項，將 Method 設為 **GET**。
4. 在 Headers 新增一筆：

   | 欄位 | 內容 |
   |---|---|
   | Key | `X-API-Key` |
   | Value | 你自己的私人 API Key |

5. 不需要設定 Request Body。

要建立 Resume、Toggle 或 Status 捷徑，只要將網址最後的路徑改成 `/resume`、`/toggle` 或 `/status` 即可。

請勿在截圖、公開分享的捷徑、Issue 或 GitHub Commit 中洩漏 API Key。更多 API 與安全說明請參閱 [`docs/API.md`](docs/API.md)。

## Serial Monitor 指令

韌體提供以下序列埠指令：

```text
help
status
p
pause
resume
swipe
bind
interval
button
hold
reset
auth reset
```

可在 Serial Monitor 輸入 `help` 查看實際指令格式與可接受參數。`auth reset` 會清除 Web Panel 的可信任瀏覽器註冊，不等於更改 API Key。

## 設定儲存與重設

- 一般設定儲存在 NVS 的 `swipe` namespace。
- Profiles 獨立儲存在 `swipe-prof` namespace。
- Web Panel 瀏覽器驗證資料獨立儲存在 `swipe-auth` namespace。
- Wi-Fi 帳密由 WiFiManager 管理。

一般設定重設並不等於完整清除裝置：不會一併清除 Wi-Fi 帳密、BLE 配對資訊、瀏覽器驗證資料或全部 Profiles。執行前請仔細閱讀 Web Panel 的確認訊息。

## 網路與安全注意事項

- ESP32 HTTP Server 使用一般 HTTP，**沒有 HTTPS 加密**。請只在可信任的區域網路使用。
- **不要將 ESP32 Web Server 的 Port Forwarding 直接開放到公網。**
- API Key 可以作為存取門檻，但 HTTP 本身不會加密資料；能監聽區域網路流量的人，仍可能取得傳輸中的 API Key 或 Session Cookie。
- 若需要遠端存取，建議使用 Tailscale 或 WireGuard 等私人 VPN，而不是直接暴露 ESP32。
- 使用獨一無二、隨機產生的 API Key。若曾經提交到 GitHub、分享或外洩，請立即更換並重新燒錄。
- Wi-Fi 設定 AP 使用專案預設密碼，請妥善控制裝置的實體存取。

## 常見問題

### `autoswipe.local` 無法開啟

部分路由器或用戶端不支援／不穩定支援 mDNS。請改用 Serial Monitor 顯示的 ESP32 IP，或到路由器 DHCP 用戶端清單查詢。

### iPhone 沒有對自動點擊做出反應

- 確認 iPhone 藍牙頁面顯示 ESP32 滑鼠已連線。
- 確認 AssistiveTouch 已啟用。
- 確認指定的滑鼠按鍵已綁定到正確動作。
- 先手動測試按鍵，必要時使用 Bind 模式。
- 確認 AutoSwipe 目前不是 Pause 狀態。

### 韌體容量超出限制

請在 Arduino IDE 選擇 **Tools → Partition Scheme → Minimal SPIFFS**，再重新編譯，並確認選擇的是預期的傳統 ESP32 Board。

### Web Panel 又要求輸入 API Key

可能是 Session 已過期或遭撤銷。請使用目前燒錄在韌體中的 API Key 重新登入。如果更換過金鑰，必須將更新後的韌體重新燒錄到 ESP32。

## 免責聲明

本專案屬於實驗性 DIY 工具。請先確認 iOS 動作設定正確，再讓裝置長時間自動執行。因裝置或 iOS 設定造成的誤觸、捲動、資料使用或其他影響，使用者需自行承擔。
