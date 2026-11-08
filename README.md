# ESP32 iOS AutoSwipe

**English** | [繁體中文](README.zh-TW.md)

An ESP32-based Bluetooth HID mouse controller for triggering iOS AssistiveTouch actions. AutoSwipe can periodically send a mouse-button click to an iPhone, allowing a configured AssistiveTouch action (for example, a scrolling or swipe gesture) to be triggered hands-free.

> This project began with the help of **Claude (Anthropic)**. **ChatGPT (OpenAI)** later joined the development and documentation work. The project direction, testing, and final decisions remain with the project owner.

> Built on [HijelHID_BLEMouse](https://github.com/HijelHub/HijelHID_BLEMouse) and [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino).

> **Project status:** Community DIY project. iOS menus and behavior may vary by iOS version. This documentation describes the V2 firmware workflow.

## Features

- Bluetooth LE HID mouse connection to iPhone.
- Configurable automatic trigger interval (minimum and maximum seconds).
- Selectable mouse button: Back, Forward, or Middle.
- Configurable click hold duration.
- Physical BOOT button controls for pause/resume and Wi-Fi setup.
- Browser-based Web Panel for runtime control and configuration.
- Five editable, collapsible profiles.
- Temporary settings or saved custom settings.
- HTTP API for Shortcuts and other LAN automations.
- API-key login and trusted-browser sessions for the Web Panel.
- Settings and profiles stored in ESP32 non-volatile storage (NVS).

## Hardware and software

### Hardware

- ESP32-WROOM-32 development board (classic ESP32; not an ESP32-S3-specific build).
- iPhone with Bluetooth and AssistiveTouch support.
- USB data cable for flashing and serial setup.

### Firmware build environment

The V2 project has been developed with:

- Arduino IDE
- Arduino-ESP32 Core 3.3.12
- NimBLE-Arduino 2.5.2
- HijelHID_BLEMouse library
- WiFiManager by tzapu

Use library/core versions compatible with the firmware source. If you use different versions, compilation or BLE behavior may differ.

## Installation

1. Open the V2 `.ino` sketch in Arduino IDE.
2. Install the required libraries listed above if they are not already installed.
3. **Set your own API key** in the firmware before compiling. Find the `API_KEY` declaration and replace the public placeholder with a long, random value. Do not publish your private key.
4. Connect the ESP32 and select the correct board and serial port.
5. Select **Tools → Partition Scheme → Minimal SPIFFS** (the tested configuration for fitting the firmware).
6. Compile and upload the sketch.
7. Open Serial Monitor at the baud rate configured in the sketch and follow the startup messages. Record the ESP32's IP address if it is printed.

The public repository should keep a placeholder in the source, not a working personal API key. Anyone who can read a public firmware source file containing a real key can reuse that key.

## First-time Wi-Fi setup

On startup, AutoSwipe attempts to connect using previously saved Wi-Fi credentials. If it cannot connect, it starts a configuration access point:

- **SSID:** `AutoSwipe-Setup`
- **Password:** `SwipeSetup27`
- **Portal:** normally available at `http://192.168.4.1/` after connecting to the setup network.

Choose your Wi-Fi network and enter its password in the portal. After saving, the ESP32 will attempt to join that network.

To reopen Wi-Fi configuration later, press and hold the ESP32 **BOOT** button for about 3 seconds. The device pauses while entering configuration mode. The setup portal has a limited timeout; if it closes, retry the long press.

> The setup AP password is a firmware default, not a substitute for network security. Do not leave the setup AP available unnecessarily, and use a trusted Wi-Fi network.

## Pairing with iPhone

1. Power the ESP32 and wait for its BLE HID mouse to advertise.
2. On the iPhone, open **Settings → Bluetooth**.
3. Find the AutoSwipe mouse device and pair/connect it.
4. Open **Settings → Accessibility → Touch → AssistiveTouch** and enable AssistiveTouch.
5. In the AssistiveTouch device/button settings, configure the connected Bluetooth mouse button to perform the action you want AutoSwipe to trigger.
6. Test the selected button manually before enabling automatic operation.

Apple changes labels and navigation between iOS versions. The exact button-customization screen may differ. AutoSwipe sends a mouse-button click; the iPhone's AssistiveTouch configuration determines what that click does.

### Button mapping

| Firmware value | Mouse button |
|---:|---|
| `0` | Back |
| `1` | Forward |
| `2` | Middle |

The Web Panel and serial commands let you select the button. Use **Bind** mode when you need repeated button events to help identify/configure the desired mouse button on iOS.

## Physical button

| BOOT button action | Function |
|---|---|
| Short press | Toggle Pause / Resume |
| Hold for about 3 seconds | Open Wi-Fi configuration portal |

## Web Panel

After the ESP32 joins your Wi-Fi network, open either:

- `http://autoswipe.local/` (if mDNS works on your network), or
- `http://<ESP32-IP>/` using the IP address printed in Serial Monitor or shown by your router.

Enter the API key configured in the firmware to sign in. The browser receives a session cookie so you do not need to enter the key for every action in that browser.

### Current Settings

The Current Settings section controls the live runtime configuration:

- Mouse button
- Minimum interval
- Maximum interval
- Click hold duration

Temporary changes affect the running device but are not saved as the persistent configuration. Use the save/persist option when you want the custom settings to survive a reboot.

### Profiles

AutoSwipe V2 provides five profiles. Each profile can store its own name and settings.

- Expand a profile to edit it; collapse it when not in use.
- **Save Changes** saves the profile's values. It does not activate that profile.
- **Apply** loads the profile into the running configuration and makes it the active saved profile.
- Resetting a profile restores that profile's defaults.
- Reset All resets all profile definitions; it does not change the currently running settings.

Saving a profile and applying a profile are deliberately separate actions. This lets you prepare or edit profiles without interrupting the current run.

### Trusted browsers

The Web Panel supports up to four trusted browser sessions. You can revoke an individual browser or revoke all trusted browsers from the panel. Revoking sessions does not change the firmware API key.

## iOS Shortcuts / HTTP API

The legacy HTTP endpoints are useful for Apple Shortcuts. They use **GET** requests and require the `X-API-Key` header.

Base URL:

```text
http://autoswipe.local
```

If mDNS is unavailable, replace the hostname with the ESP32's IP address, for example `http://192.168.1.50`.

Available endpoints:

| Method | Path | Action |
|---|---|---|
| GET | `/status` | Read device status |
| GET | `/pause` | Pause automatic triggering |
| GET | `/resume` | Resume automatic triggering |
| GET | `/toggle` | Toggle pause/resume |

For Apple Shortcuts, add **Get Contents of URL**, set the method to **GET**, and add this header:

| Header field | Value |
|---|---|
| Key | `X-API-Key` |
| Value | Your private firmware API key |

Example URL:

```text
http://autoswipe.local/pause
```

Create separate shortcuts by changing the final path to `/resume`, `/toggle`, or `/status`. Keep the key private; do not include it in screenshots, shared shortcuts, public issue reports, or repository commits.

See [`docs/API.md`](docs/API.md) for the endpoint and security overview.

## Serial Monitor commands

The firmware includes these serial commands:

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

Use `help` in Serial Monitor for the firmware's command syntax and accepted arguments. `auth reset` clears trusted Web Panel browser registrations; it is not the same as changing the API key.

## Persistence and reset behavior

- Main settings are stored in NVS under the `swipe` namespace.
- Profile data is stored separately under `swipe-prof`.
- Web Panel trusted-browser authentication data is stored separately under `swipe-auth`.
- Wi-Fi credentials are managed by WiFiManager.

A normal settings reset is not a factory erase: it does not clear Wi-Fi credentials, BLE pairing information, browser authentication, or all profile data. Check the confirmation text in the Web Panel before proceeding.

## Network and security notes

- The device's HTTP server uses plain HTTP, not HTTPS. Use it only on a trusted local network.
- **Do not port-forward the ESP32 web server to the public internet.**
- A LAN API key is a useful access gate, but it does not encrypt HTTP traffic. Anyone able to observe unencrypted traffic on the network may be able to capture the key or session cookie.
- For remote access, prefer a private VPN such as Tailscale or WireGuard rather than exposing the ESP32 directly.
- Use a unique, randomly generated API key. Rotate it if it was ever committed, shared, or exposed.
- The Wi-Fi setup AP password is a project default; keep physical access to the device controlled.

## Troubleshooting

### `autoswipe.local` does not open

Some routers and client devices do not resolve mDNS hostnames reliably. Use the ESP32 IP address from Serial Monitor or your router's DHCP client list.

### iPhone does not respond to automatic clicks

- Confirm the ESP32 is paired/connected in iPhone Bluetooth settings.
- Confirm AssistiveTouch is enabled.
- Confirm the selected mouse button is assigned to the intended AssistiveTouch action.
- Test the button manually and try Bind mode if needed.
- Check that AutoSwipe is not paused.

### Firmware does not fit

Select **Tools → Partition Scheme → Minimal SPIFFS** and compile again. Ensure the selected board is the intended classic ESP32 target.

### Web Panel asks for the API key again

The session may have expired or been revoked. Sign in again with the API key currently compiled into the firmware. If you changed the key, reflash the device with the updated firmware.

## Disclaimer

This is an experimental DIY project. Use it responsibly and test the configured iOS action before leaving it unattended. The author is not responsible for unintended taps, scrolling, data usage, or other effects caused by device or iOS configuration.

## Credits and acknowledgements

- **Project creator and maintainer:** [stJL](https://github.com/stJL), who shaped the idea, hardware setup, testing, and project direction.
- **Claude (Anthropic):** helped initiate the project and contributed to its design, implementation, and debugging.
- **ChatGPT (OpenAI):** joined later and helped with continued development, Web Panel and Profiles discussions, API/documentation work, and troubleshooting.
- [HijelHub/HijelHID_BLEMouse](https://github.com/HijelHub/HijelHID_BLEMouse): BLE HID mouse library. Please review its license and README, including any commercial-use notes.
- [h2zero/NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino): BLE stack library.

This project is a collaborative effort: thank you to the people and open-source projects that helped make it possible.
