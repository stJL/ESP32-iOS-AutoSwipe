# AutoSwipe V2 HTTP API and Security

## Base URL

Use the mDNS hostname when available:

```text
http://autoswipe.local
```

Otherwise use the ESP32's LAN IP address, for example `http://192.168.1.50`.

## Legacy API (API-key header)

These compatibility endpoints use HTTP GET and require the `X-API-Key` request header.

| Method | Path | Description |
|---|---|---|
| GET | `/status` | Read device status |
| GET | `/pause` | Pause automatic triggering |
| GET | `/resume` | Resume automatic triggering |
| GET | `/toggle` | Toggle pause/resume |

Example request:

```http
GET /pause HTTP/1.1
Host: autoswipe.local
X-API-Key: YOUR_PRIVATE_API_KEY
```

Do not put a real key in documentation, source control, screenshots, or shared examples.

## Web Panel API and session model

The Web Panel is served from `/`.

1. The browser submits the configured API key to `POST /api/auth` (along with a browser/device label).
2. On successful authentication, the device issues an `AutoSwipeSession` cookie.
3. The cookie is HttpOnly, SameSite=Strict, scoped to `/`, and configured with a 30-day maximum age.
4. Subsequent `/api/*` operations use the session cookie rather than repeatedly transmitting the API key.
5. The firmware stores hashed session tokens in its authentication NVS namespace and supports up to four trusted browsers.

The authenticated panel can reveal the firmware's configured API key only after an explicit user action. Treat that display as sensitive.

### Web Panel routes

The V2 Web Panel uses the following routes:

| Method | Path | Purpose |
|---|---|---|
| POST | `/api/auth` | Authenticate a browser and establish a session |
| POST | `/api/logout` | Revoke the current browser session |
| GET | `/api/status` | Read device status, settings, profiles, and trusted-browser information |
| POST | `/api/apikey` | Reveal the configured API key to an authenticated session |
| POST | `/api/pause` | Pause automatic triggering |
| POST | `/api/resume` | Resume automatic triggering |
| POST | `/api/toggle` | Toggle pause/resume |
| POST | `/api/swipe` | Trigger a manual swipe/click action |
| POST | `/api/settings` | Update runtime settings; `persist=1` saves them |
| POST | `/api/bind` | Enable or disable bind mode |
| POST | `/api/reset` | Reset current settings |
| POST | `/api/profiles/apply` | Apply a profile (`slot` 0–4) |
| POST | `/api/profiles/save` | Save a profile (`slot`, `name`, and settings) |
| POST | `/api/profiles/rename` | Rename a profile |
| POST | `/api/profiles/reset` | Reset one profile (`slot`) |
| POST | `/api/profiles/reset-all` | Reset all profile definitions |
| POST | `/api/revoke` | Revoke one trusted browser (`slot`) |
| POST | `/api/revoke-all` | Revoke all trusted browsers and expire the current cookie |

The Web Panel routes are session-protected. They are not intended as a public, unauthenticated API. Request fields and response JSON are implementation details and may change between firmware revisions; consult the matching firmware source before building third-party clients against these routes.

## Settings fields

The settings interface includes:

| Field | Meaning |
|---|---|
| `button` | Mouse button selection: `0` Back, `1` Forward, `2` Middle |
| `minSec` | Minimum automatic interval in seconds |
| `maxSec` | Maximum automatic interval in seconds |
| `holdMs` | Mouse-button hold duration in milliseconds |
| `persist` | When set to `1` on the settings update, save the settings persistently |

A non-persistent settings update changes the current runtime configuration only. A persistent custom-settings update saves the values and sets the active profile marker to custom settings (`activeProfile = -1`).

## Security considerations

- The embedded HTTP server uses plain HTTP. API keys and cookies are not encrypted in transit.
- Keep the device on a trusted LAN. Do not expose its HTTP port to the public internet using router port forwarding.
- For remote access, use a private VPN (for example, Tailscale or WireGuard).
- Generate a unique, high-entropy API key and keep it out of public repositories.
- If a key has been exposed, replace it in the private firmware source and reflash the ESP32. Updating a public README alone does not rotate the key.
- Revoke untrusted browser sessions in the Web Panel. The serial command `auth reset` can clear trusted browser registrations.
- Session cookies are bearer credentials. Do not share browser profiles or copy session cookies into issue reports.

## Apple Shortcuts example

Use the **Get Contents of URL** action with method **GET** and add this header:

```text
X-API-Key: YOUR_PRIVATE_API_KEY
```

Example:

```text
http://autoswipe.local/pause
```

Replace `/pause` with `/resume`, `/toggle`, or `/status` as needed.
