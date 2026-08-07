# User guide

[Русская версия](USER.md)

**Lyrics AI Translator** for foobar2000 v2 (**32-bit or 64-bit** — the package must match your foobar build).

## Features

- Shows lyrics for the current track in a dedicated window
- Highlights the current line when the cache contains synced timestamps (LRC/sync)
- Fetches lyrics from lrclib.net and translates them via an LLM (OpenAI-compatible API)
- Stores results in a local cache folder (JSON)
- Optionally syncs the cache with a Git repository (GitHub/GitLab, etc.)
- Optionally exposes a LAN web page for another device: lyrics, QR code, player controls

While the lyrics window is **closed**, the plugin does not follow playback and does not start the worker/web processes.

---

## Installation

### From a `.fb2k-component` package (recommended)

Pick the file that matches your foobar2000 bitness:

| foobar2000 | File |
|------------|------|
| 32-bit (x86) | `foo_lyrics_ai_translator_win32.fb2k-component` |
| 64-bit (x64) | `foo_lyrics_ai_translator_win64.fb2k-component` |

The package already includes the DLL, `lyrics_worker.exe`, `lyrics_server.exe`, and `config.example.json`.

1. **File → Preferences → Components → Install…**
2. Select the appropriate `.fb2k-component`.
3. Fully restart foobar2000.
4. Confirm the component appears under **Preferences → Components**.
5. Configure via **Preferences → Tools → Lyrics AI Translator**  
   (the plugin creates/updates `config.json` next to the DLL)  
   **or** copy `config.example.json` → `config.json` in the component folder and edit it manually.

Typical component folder locations:

- Standard install: `%APPDATA%\foobar2000-v2\user-components\…`
- Portable / x64: your own `profile\user-components` / `user-components-x64` next to foobar

The **Open plugin folder** button on Preferences tabs opens that directory.

### Manual install (folder)

If you install from `dist\x86\lyrics-ai-translator\` or `dist\x64\lyrics-ai-translator\` without Install…:

1. Copy the **entire** folder into your install’s `user-components` directory (folder name is arbitrary; the DLL inside must remain `foo_lyrics_ai_translator.dll` — **do not rename it**).
2. Configure via Preferences or `config.example.json` → `config.json`.
3. Fully restart foobar2000.

Folder contents:

| File | Purpose |
|------|---------|
| `foo_lyrics_ai_translator.dll` | plugin |
| `lyrics_worker.exe` | lyrics fetch / translation |
| `lyrics_server.exe` | web UI (Web remote) |
| `config.example.json` | sample settings |

### Important

- Package bitness must equal foobar2000 bitness (**do not mix** x86/x64).
- Do not rename the DLL.
- Secrets (API key, PAT, passwords) live in `config.json`.

---

## Initial setup

Open settings via either:

- **View → Lyrics AI Translator → Preferences…**
- or **File → Preferences → Tools → Lyrics AI Translator**

Tabs:

### Language

- **Enable translation** — whether to run translation
- **Target language** — translation language (presets + custom value)

### LLM

- **Base URL** — e.g. `https://api.openai.com/v1`
- **Model** — API model name
- **API key** — provider key (**required** to fetch/translate lyrics)

### Proxy

Optional proxy for worker requests (**both LRCLib and LLM** use the same HTTP client). **Enabled by default**, type **SOCKS5**.

In Preferences: **Enable proxy**, then **Type** — **SOCKS5** or **HTTP**.

- **SOCKS5** — typical VPN / local SOCKS
- **HTTP** — HTTP CONNECT (often called an “HTTP/HTTPS proxy”)
- Host / Port / User / Password
- Clear **Enable proxy** for direct connections

In `config.json`:

```json
"proxy": {
  "enabled": true,
  "type": "socks5",
  "url": "127.0.0.1",
  "port": "1080",
  "user": "",
  "password": ""
}
```

`"type": "http"` selects HTTP CONNECT. If `type` is omitted — SOCKS5 (or HTTP if `url` uses an `http://` / `https://` scheme).

### Cache

- **Cache folder** — where lyrics/translations are stored (`temp` next to the plugin by default). Changing the path **does not migrate** existing files.
- **Enable Git sync** — share a lyrics library across machines using a Git remote as storage:
  - Remote repository URL
  - PAT (GitHub/GitLab token)
  - Branch
  - Pull on startup / Auto setup

With sync off, the plugin only reads/writes the local folder.

### Web

- **Enable web remote control** — LAN page for a phone (or other device)
- **Port** — default `8765`
- **Auth token (optional)**:
  - **empty** — anyone on your Wi‑Fi who knows the URL can open it
  - **set** (Generate button) — access only via a URL with `?token=...` (the lyrics window shows that URL/QR)

Web runs while the lyrics window is open and `lyrics_server.exe` is present next to the DLL.

---

## Usage

### Lyrics window

**View → Lyrics AI Translator → Open lyrics window**

- On track change, the plugin looks for a JSON file in the cache.
- If none is ready, it starts `lyrics_worker.exe` (LLM settings and network/proxy required).
- Status bar: load state; with Web enabled — URL and a hint to click for the QR code.

### Batch fetch

The **Get all lyrics** button prefetches lyrics for tracks in the playlist: skips already-ready items, tracks without album, and duplicates; **Cancel** aborts.

### Sync highlight toggle

The Sync button in the upper-right corner disables auto-highlight/scroll so you can scrub through the lyrics manually.

### Mobile device (Web)

1. Enable Web in Preferences, Apply.
2. Open the lyrics window.
3. Click the Web status line → QR / URL.
4. Open the link on the device (same LAN as the PC).
5. On the page: lyrics, highlight, prev / play-pause / next, ±10 s.

If an auth token is set, scan the QR from the window (it already includes `?token=`). A bare `http://IP:PORT/` without the token returns unauthorized.

### Git sync

When enabled: on startup (after a delay) the plugin may pull; when new lyrics appear it may push to the remote. Requires `git` on PATH, a remote URL, and a PAT with repository access.

---

## Troubleshooting

| Symptom | Check |
|---------|--------|
| Component missing from the list | foobar bitness = package? DLL name exactly `foo_lyrics_ai_translator.dll`? Restart? |
| No lyrics / LLM error | API key, base URL, model; proxy type/host; foobar Console |
| Worker does not start | Is `lyrics_worker.exe` next to the DLL? |
| Web / QR broken | Web enabled? Lyrics window open? `lyrics_server.exe` present? Firewall on the port? Same Wi‑Fi? |
| Unauthorized in the browser | Token set — open the URL with `?token=` from status/QR |
| Sync does not pull/push | Is `git` installed? remote/PAT/branch? Cache folder = repo dir after Apply? |

Logs: **View → Console** in foobar2000. Optionally enable file logging under Advanced.

---

## Where data lives

- Settings: `config.json` **next to the DLL** (component folder)
- Lyrics cache: folder from **Cache folder** (default `temp` under/near the plugin install)

Cache layout: `<cacheDir>/<artist>/<album>/<title>.json`
