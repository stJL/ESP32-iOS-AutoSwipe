# ESP32 iOS AutoSwipe（繁體中文）

[English](README.md) | **繁體中文**

讓 ESP32 變成藍牙（BLE）滑鼠，每隔隨機時間按一次滑鼠按鈕。在 iPhone 上用 **AssistiveTouch（輔助觸控）** 把這顆按鈕綁定到任何動作，例如你錄製的滑動手勢或向下捲動，就能達到免動手、隨機時間的自動滑動。

> 基於 [HijelHID_BLEMouse](https://github.com/HijelHub/HijelHID_BLEMouse) 與 NimBLE-Arduino 製作。
> **本專案由 Claude（Anthropic）協助撰寫。**

---

## 運作原理

```
ESP32（BLE 滑鼠）  --按下 Back 鍵-->  iPhone AssistiveTouch  -->  你綁定的動作
   每 29～39 秒                        （自訂按鈕）               （滑動手勢 / 捲動）
```

ESP32 不會移動游標，只負責按下一顆滑鼠按鈕，iOS 會執行你綁在那顆按鈕上的動作。錄製的自訂手勢帶有螢幕上的絕對座標，所以游標位置不影響它。

## 適用情境

任何靠滑動換頁的 App 都可以這樣驅動，例如：

- 電子書、相簿與投影片
- 簡報與電子看板
- 資訊流類 App，包含短影音 App（作者實際測試用的是其中一款，效果會因 App 與 iOS 版本而異）
- 無障礙：減少重複滑動的需求

請遵守你所自動化操作的 App 之服務條款。

## 需求

| 項目 | 版本 |
|---|---|
| 有 BLE 的 ESP32 開發板 | 除了 ESP32-S2 與 ESP32-P4 以外都可以 |
| [ESP32 Arduino core](https://github.com/espressif/arduino-esp32) | 3.x |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | >= 2.3.8 |
| [HijelHID_BLEMouse](https://github.com/HijelHub/HijelHID_BLEMouse) | 最新版（程式庫管理員搜尋 "HijelHID"） |
| iPhone | 必須開啟 AssistiveTouch |

儲存設定用的 `Preferences` 已內建在 ESP32 core 裡，不需要另外安裝。

## 安裝

1. 在 Arduino IDE 安裝上面三個程式庫。
2. 開啟 `ESP32_iOS_AutoSwipe/ESP32_iOS_AutoSwipe.ino`。
3. 選擇你的 ESP32 開發板並上傳。
4. 開啟序列埠監控視窗，鮑率 **115200**（行結尾選 *Newline* 最穩定，選 *No line ending* 也能運作）。

## iPhone 端設定

不同 iOS 版本的選單名稱可能略有差異。

1. **開啟 AssistiveTouch：** 設定 → 輔助使用 → 觸控 → AssistiveTouch → 開啟。
2. **配對 ESP32：** 設定 → 藍牙 → 點選 **BLE Mouse**，序列埠會顯示 `iPhone paired!`。
3. **準備要觸發的動作。** 建議：用 AssistiveTouch 的「建立新手勢」，照你平常滑動的速度與距離錄一個手勢。也可以用內建動作，例如向下捲動。
4. **綁定按鈕：**
   1. 在序列埠輸入 `bind on`，ESP32 會每 2 秒送一次 Back 鍵。
   2. 在 iPhone：AssistiveTouch → 裝置 → BLE Mouse → **自訂按鈕**。
   3. 等 iOS 偵測到按鈕後，選擇你要的動作。
   4. 輸入 `bind off`。
5. **測試：** 輸入 `swipe`（或 `b`），iPhone 應該會執行動作。
6. 完成。ESP32 之後會以隨機間隔（預設 29～39 秒）重複執行動作。

## 序列埠指令

| 指令 | 說明 |
|---|---|
| `help`、`h`、`?` | 顯示指令列表 |
| `status`、`s` | 顯示連線狀態與設定 |
| `p` | 暫停 / 繼續（切換） |
| `pause` / `resume` | 明確暫停 / 繼續 |
| `swipe`、`b` | 立刻手動送出一次滑動 |
| `bind [on\|off]`、`k` | 綁定模式：每 2 秒送一次按鈕，讓 iOS 辨識 |
| `interval <min> <max>` | 滑動間隔範圍（秒），例：`interval 25 45` |
| `button <name>` | 要按哪顆滑鼠按鈕：`back`、`forward` 或 `middle` |
| `hold <ms>` | 按鈕按住時間，10～500 毫秒（預設 50） |
| `reset` | 恢復預設值 |

**BOOT 鍵**（GPIO 0）也可以切換暫停 / 繼續。如果你的開發板 BOOT 鍵在其他腳位，請修改程式開頭的 `PAUSE_BUTTON_PIN`。

所有設定都會自動存進 flash（NVS），重新開機或重新上傳程式後仍然有效。

## `button` 與 `hold` 是做什麼的

- **`button <name>`**：滑鼠有好幾顆按鈕，iOS 是**依按鈕來綁定動作**的。ESP32 按下你在這裡選的那一顆，iOS 就執行綁在那顆上的動作。預設的 `back` 對大多數人都適用。如果換成別顆，**要在 iOS 重新綁定**（見步驟 4）。
- **`hold <ms>`**：按一次按鈕包含「按下」和「放開」兩個事件，`hold` 就是兩者之間的時間（預設 50 毫秒）。如果 iOS 偶爾沒反應，調長一點，例如 `hold 100`；如果 iOS 反應了兩次，就調短。

## 小提醒與疑難排解

- **iPhone 完全沒反應：** 確認 AssistiveTouch 已開啟，iOS 需要它才會接受藍牙滑鼠。
- **綁定時 iOS 偵測不到按鈕：** 保持 `bind on`，確認序列埠每 2 秒出現一次 `Bind click sent`，並試試 `hold 100`。
- **偶爾漏按：** 調大 `hold`。
- **改了程式裡的預設間隔卻沒變化：** flash 裡儲存的值會優先於程式碼，輸入一次 `reset` 就會套用新預設值。
- **重新配對：** 如果配對卡住，先到 iPhone 的藍牙設定移除 "BLE Mouse" 再重新配對。
- **測試時先暫停：** 輸入 `p` 或按 BOOT 鍵，避免自動滑動打斷你。

## 背景：為什麼用按鈕綁定？

開發過程中，用某個短影音 App 試過另外兩種做法：

- **滑鼠滾輪事件**（`scroll` / `addScroll`）：成功率受格數影響，而且落在門檻附近，結果不穩定。這類 App 是用觸控的位移與放開速度判斷翻頁，滾輪沒有這些資訊。
- **游標按住拖曳：** 該 App 不接受。

改成讓 iOS 透過 AssistiveTouch 重播真實的觸控手勢，就避開了這兩個問題。以上是作者在單一 App 上的測試結果，實際情況會因 App 與 iOS 版本而異。

## 常見問題

**可以用在短影音 App 嗎？**
原則上可以：任何靠滑動換頁的 App 都適用。作者在其中一款 App 上測試過，但效果會因 App 與 iOS 版本而異。請遵守該 App 的服務條款。

**它會移動游標嗎？**
不會。ESP32 只負責按下滑鼠按鈕，iOS 會執行你綁定的動作。錄製的自訂手勢使用螢幕上的絕對座標，所以游標位置不影響它。如果你綁定的是依游標位置執行的內建動作，使用前請先測試。

**Android 可以用嗎？**
尚未測試。BLE 滑鼠程式庫本身支援 Android，但本文的按鈕綁定步驟是 iOS AssistiveTouch 專用的。

**為什麼不直接送滾輪事件？**
很多滑動換頁的 App 是用觸控的位移與放開速度判斷翻頁，滾輪沒有這些資訊。詳見[背景](#背景為什麼用按鈕綁定)。

**iPhone 螢幕需要保持亮著嗎？**
需要，手勢只有在螢幕亮著且已解鎖時才能作用在 App 上。長時間使用時，可以考慮調整 iPhone 顯示設定中的自動鎖定時間。

**我的 iPhone 完全沒反應。**
請確認 AssistiveTouch 已開啟，且按鈕已綁定。詳見[小提醒與疑難排解](#小提醒與疑難排解)。

## 測試環境

> 請填入你自己的環境，例如：
> - 開發板：ESP32-WROOM-32 DevKit
> - ESP32 Arduino core：3.3.12
> - NimBLE-Arduino：2.5.2
> - iPhone / iOS：iPhone 15 Pro (ios 27.2)

## 免責聲明

使用風險自負。請遵守你所自動化操作的 App 之服務條款，以及適用的法律。

## 致謝

- [HijelHub/HijelHID_BLEMouse](https://github.com/HijelHub/HijelHID_BLEMouse)：BLE HID 滑鼠程式庫（Apache-2.0），商業使用相關說明請參閱其 README。
- [h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)
- **Claude（Anthropic）** 協助設計、撰寫與除錯本專案。
