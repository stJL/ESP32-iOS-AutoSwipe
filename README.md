# ESP32 iOS AutoSwipe

**English** | [繁體中文](README.zh-TW.md)

Turn an ESP32 into a Bluetooth Low Energy (BLE) mouse that presses a mouse button at random intervals. On the iPhone, **AssistiveTouch** maps that button to any action you like, such as a recorded swipe gesture or Scroll Down. The result is a hands-free, randomly timed swipe.

> Built on [HijelHID_BLEMouse](https://github.com/HijelHub/HijelHID_BLEMouse) and NimBLE-Arduino.
> **This project was written with the help of Claude (Anthropic).**

---

## How it works

```
ESP32 (BLE mouse)  --press Back button-->  iPhone AssistiveTouch  -->  your chosen action
   every 29-39 s                            (Customize Buttons)         (swipe gesture / scroll)
```

The ESP32 does not move the cursor. It only presses one mouse button, and iOS runs whatever action you bound to that button. A recorded custom gesture contains absolute screen coordinates, so the cursor position does not matter for it.

## Use cases

Any app that advances with a swipe can be driven this way, for example:

- E-readers, photo viewers and slideshows
- Presentations and digital signage
- Feed-style apps, including short-video apps (the author's own testing was done with one such app; results vary by app and iOS version)
- Accessibility: reducing the need for repeated swiping

Please respect the terms of service of the apps you automate.

## Requirements

| Item | Version |
|---|---|
| ESP32 board with BLE | All variants except ESP32-S2 and ESP32-P4 |
| [ESP32 Arduino core](https://github.com/espressif/arduino-esp32) | 3.x |
| [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) | >= 2.3.8 |
| [HijelHID_BLEMouse](https://github.com/HijelHub/HijelHID_BLEMouse) | Latest (Library Manager: search "HijelHID") |
| iPhone | AssistiveTouch must be enabled |

`Preferences` (used to save settings) is part of the ESP32 core, so there is nothing extra to install.

## Installation

1. Install the three libraries above in the Arduino IDE.
2. Open `ESP32_iOS_AutoSwipe/ESP32_iOS_AutoSwipe.ino`.
3. Select your ESP32 board and upload.
4. Open the Serial Monitor at **115200 baud** (line ending: *Newline* is the most reliable; *No line ending* also works).

## Setup on the iPhone

Menu names can differ slightly between iOS versions.

1. **Enable AssistiveTouch:** Settings → Accessibility → Touch → AssistiveTouch → On.
2. **Pair the ESP32:** Settings → Bluetooth → tap **BLE Mouse**. The Serial Monitor shows `iPhone paired!`.
3. **Prepare the action you want to trigger.** Recommended: record a custom gesture at the speed and distance of your real swipe (AssistiveTouch → Create New Gesture). You can also use a built-in action such as Scroll Down.
4. **Bind the button:**
   1. Type `bind on` in the Serial Monitor. The ESP32 now sends the Back button every 2 seconds.
   2. On the iPhone: AssistiveTouch → Devices → BLE Mouse → **Customize Buttons**.
   3. Wait until iOS detects the button, then choose your action.
   4. Type `bind off`.
5. **Test it:** type `swipe` (or `b`). The iPhone should perform the action.
6. Done. The ESP32 now repeats the action at random intervals (default 29–39 s).

## Serial commands

| Command | Description |
|---|---|
| `help`, `h`, `?` | Show the command list |
| `status`, `s` | Show connection state and settings |
| `p` | Toggle pause / resume |
| `pause` / `resume` | Explicit pause / resume |
| `swipe`, `b` | Send one swipe immediately |
| `bind [on\|off]`, `k` | Bind mode: sends the button every 2 s so iOS can detect it |
| `interval <min> <max>` | Swipe interval in seconds, e.g. `interval 25 45` |
| `button <name>` | Which mouse button to press: `back`, `forward`, or `middle` |
| `hold <ms>` | How long the button is held, 10–500 ms (default 50) |
| `reset` | Restore defaults |

The **BOOT button** (GPIO 0) also toggles pause / resume. On boards where BOOT is on a different pin, change `PAUSE_BUTTON_PIN` at the top of the sketch.

All settings are saved to flash (NVS) and survive reboots and re-uploads.

## What `button` and `hold` do

- **`button <name>`**: the mouse has several buttons, and iOS binds actions **per button**. The ESP32 presses whichever button you choose here, and iOS runs the action bound to it. The default `back` is fine for most people. If you switch to another button, **re-bind it in iOS** (see step 4).
- **`hold <ms>`**: a button press has a "down" and an "up" event, and `hold` is the time between them (default 50 ms). If iOS sometimes ignores the press, raise it, e.g. `hold 100`. If iOS reacts twice, lower it.

## Tips and troubleshooting

- **Nothing happens on the iPhone:** make sure AssistiveTouch is on. It is required for iOS to accept a Bluetooth mouse.
- **iOS does not detect the button during binding:** keep `bind on` running and make sure the Serial Monitor shows `Bind click sent` every 2 seconds. Try `hold 100`.
- **The press is sometimes missed:** increase `hold`.
- **Edited the default interval in the code but nothing changed:** values saved in flash override the code. Type `reset` once to load the new defaults.
- **Re-pairing:** if pairing gets stuck, remove "BLE Mouse" in the iPhone's Bluetooth settings and pair again.
- **Pause while testing:** type `p` or press BOOT so the automatic swipe does not interrupt you.

## Background: why a mapped button?

During development, two other approaches were tried with a short-video app:

- **Scroll wheel events** (`scroll` / `addScroll`): the success rate depended on the number of clicks and sat near a threshold, so results were inconsistent. These apps judge a page change by touch distance and release speed, which a wheel does not provide.
- **Click-and-drag with the cursor:** the app did not accept it.

Letting iOS replay a real touch gesture through AssistiveTouch avoided both problems. This was observed in the author's own testing with one app. Your results may vary by app and iOS version.

## FAQ

**Can I use this with short-video apps?**
In principle, yes: it works with any app that advances with a swipe. The author tested it with one such app, but results depend on the app and iOS version. Please respect the app's terms of service.

**Does it move the cursor?**
No. The ESP32 only presses a mouse button, and iOS runs the action you bound to it. A recorded custom gesture uses absolute screen coordinates, so cursor position does not matter. If you bind a built-in action that depends on the cursor position, test it before relying on it.

**Does it work on Android?**
Not tested. The BLE mouse library itself supports Android, but the button-mapping steps in this guide are specific to iOS AssistiveTouch.

**Why not just send scroll-wheel events?**
Many swipe-to-next apps judge a page change by touch distance and release speed, which a scroll wheel does not provide. See [Background](#background-why-a-mapped-button).

**Does the iPhone screen have to stay on?**
Yes, a gesture can only affect the app while the screen is on and unlocked. For long sessions, consider adjusting Auto-Lock in the iPhone's display settings.

**It does nothing on my iPhone.**
Make sure AssistiveTouch is enabled and the button is bound. See [Tips and troubleshooting](#tips-and-troubleshooting).

## Tested on

> - Board: ESP32-WROOM-32 DevKit
> - ESP32 Arduino core: 3.3.12
> - NimBLE-Arduino: 2.5.2
> - iPhone / iOS: iPhone 15 Pro (ios 27.2)

## Disclaimer

Use at your own risk. Please respect the terms of service of any app you automate, and the laws that apply to you.

## Credits

- [HijelHub/HijelHID_BLEMouse](https://github.com/HijelHub/HijelHID_BLEMouse): the BLE HID mouse library (Apache-2.0). Please read its README for its commercial-use note.
- [h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)
- **Claude (Anthropic)** helped design, write and debug this project.
