# Changelog

All notable project changes are documented here.

## V2 — Web Panel and Profiles

- Added a browser-based Web Panel for device status and runtime control.
- Added API-key authentication for initial Web Panel login.
- Added trusted-browser sessions with individual and global revocation; supports up to four trusted browsers.
- Added an authenticated API-key reveal action in the Web Panel.
- Added five independently editable Profiles.
- Made Profiles collapsible to keep the interface compact.
- Separated saving a profile from applying it to the running device.
- Added temporary runtime settings and persistent custom settings.
- Added profile reset and reset-all controls.
- Added HTTP API routes for status, pause, resume, toggle, manual trigger, settings, bind mode, and profile management.
- Retained the simple API-key-header GET endpoints for Shortcuts compatibility: `/status`, `/pause`, `/resume`, and `/toggle`.
- Added browser-side copy support for copying the device URL and API key.

## Earlier firmware

Earlier firmware provided BLE HID mouse triggering, interval/button/hold configuration, serial commands, physical BOOT-button controls, and Wi-Fi provisioning. Refer to the repository history for changes in earlier revisions.
