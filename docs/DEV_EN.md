# Developer guide

[Русская версия](DEV.md)

## Architecture

```
foobar2000
  └─ foo_lyrics_ai_translator.dll
        ├─ lyrics window / Preferences / config.json
        ├─ launches lyrics_worker.exe   → writes JSON under cacheDir
        └─ launches lyrics_server.exe   → LAN UI (while the window is relevant)
              ↑ POST /api/state from the PC
              ↓ GET page / API from the phone (+ optional ?token=)
```

| Component | Language | Role |
|-----------|----------|------|
| `plugin/` | C++ (foobar SDK, WTL) | UI, playback hooks, cache reads, process launch |
| `worker/` | Go | lrclib + LLM, write cache JSON |
| `lyrics_server/` | Go | HTTP + SSE/web UI, QR, player commands |

Single config file: `config.json` next to the DLL. Preferences read/write it via `plugin_config`.

The worker is **not** a long-running service: it starts on demand, writes a file, and exits.

The web server stays alive while the plugin keeps the process (currently tied to the lyrics window / web-enabled lifecycle).

**Bitness:** DLL, worker, and server in one package must match (x86↔386 or x64↔amd64). Do not mix.

---

## Repository tree

```
.
├── plugin/foo_lyrics_plugin/   # DLL sources, .rc, Preferences
├── worker/                     # lyrics_worker
├── lyrics_server/              # lyrics_server + embedded webui
├── scripts/package-release.ps1 # Go bins → dist\<arch>\
├── build-release.bat           # MSBuild DLL → out\ + dist\
├── lyrics-plugin.sln           # platforms: x86, x64
├── Directory.Build.props
├── config.example.json
├── docs/
├── out\Win32|x64\              # DLL after MSBuild (not in git)
└── dist\x86|x64\lyrics-ai-translator\  # full package (not in git)
```

---

## Dependencies

### Required to build the DLL

1. **foobar2000 SDK** (this project uses `SDK-2025-03-07`)  
   The solution references the SDK **one level above** the repository root:

   ```
   parent/
     this-repo/          ← git clone (this repository)
     SDK-2025-03-07/     ← SDK beside it, not inside git
   ```

   Paths in `lyrics-plugin.sln`: `..\SDK-2025-03-07\...`  
   If your SDK lives elsewhere, adjust the solution/vcxproj paths.

2. **WTL 10** (headers). Default: `C:\Tools\WTL10\Include`  
   Override with env `WTLIncludeDir`.

3. **Visual Studio / Build Tools** with C++:
   - Desktop C++ workload
   - toolset **v145** (VS 2026)
   - **C++ ATL** for the toolset (x86 **and** x64)
   - Windows SDK

4. **Go** (for worker and lyrics_server):
   - worker: see `worker/go.mod` (Go 1.22+)
   - server: see `lyrics_server/go.mod`
   - `GOOS=windows` + `GOARCH=386` (x86 package) or `amd64` (x64 package), `CGO_ENABLED=0`

### Runtime (end user)

- foobar2000 v2 of the **same bitness** as the package
- `git` on PATH — only if sync is used
- network / proxy (**SOCKS5** or **HTTP CONNECT**) — for the worker; SOCKS4 is not supported

---

## Build

### DLL

```bat
build-release.bat              rem x86 (default)
build-release.bat x64
build-release.bat all          rem both architectures
build-release.bat x86 smoke
```

- MSBuild `lyrics-plugin.sln` Release|x86 or Release|x64
- DLL → `out\Win32\` or `out\x64\`
- Copied into `dist\<arch>\lyrics-ai-translator\`
- `.fb2k-component`: `dist\foo_lyrics_ai_translator_win32.fb2k-component` / `_win64`

### Full package (DLL + Go)

Build the DLL first, then:

```powershell
.\scripts\package-release.ps1              # x86
.\scripts\package-release.ps1 -Arch x64
.\scripts\package-release.ps1 -Arch all
```

Produces `lyrics_worker.exe`, `lyrics_server.exe`, `config.example.json`, and `INSTALL.txt` under `dist\<arch>\lyrics-ai-translator\`.

### Worker only

```powershell
cd worker
$env:GOOS="windows"; $env:GOARCH="386"; $env:CGO_ENABLED="0"   # or amd64 for x64
go build -ldflags="-s -w" -o ..\dist\x86\lyrics-ai-translator\lyrics_worker.exe .\cmd\lyrics_worker
```

Manual run:

```powershell
.\lyrics_worker.exe -config path\to\config.json
```

For a manual run the config must include `track.artist` / `title` / (preferably) `album`.

### lyrics_server only

```powershell
cd lyrics_server
$env:GOOS="windows"; $env:GOARCH="386"; $env:CGO_ENABLED="0"   # or amd64
go build -ldflags="-s -w" -o ..\dist\x86\lyrics-ai-translator\lyrics_server.exe .\cmd\lyrics_server
```

```powershell
.\lyrics_server.exe -host 0.0.0.0 -port 8765
# optional: -token secret
```

### Smoke DLL build

```bat
build-release.bat x86 smoke
build-release.bat x64 smoke
```

---

## Configuration

Field source of truth: `config.example.json` + `plugin_config.cpp` / Preferences.

Main blocks:

| Block | Purpose |
|-------|---------|
| `llm` | baseUrl, model, apiKey |
| `proxy` | `enabled`, `type` (`socks5`\|`http`), `url`/`port`/`user`/`password`; both LRCLib and LLM go through one `httpclient`; `enabled: false` = direct; missing `type` → socks5 (or http if the url scheme is http/https). No SOCKS4 |
| `targetLang` / `enableTranslation` | translation language |
| `cacheDir` | JSON cache root |
| `sync` | Git remote, PAT, branch, pullOnStartup, autoSetup; on Preferences Apply, `repoDir` = cacheDir |
| `web` | enabled, host, port, authToken (empty = no auth), updateIntervalMs |

Empty `web.authToken`: authentication is off — `/`, `/api/state` (GET), and SSE are open.  
Non-empty: browser endpoints accept the token via query `?token=…` or cookie `lyrics_token`. The plugin’s loopback API (`POST /api/state`, highlight/player polls) does not require auth.

---

## Key plugin modules

| File | Contents |
|------|----------|
| `lyrics_window.cpp` | Window UI, cache, worker queue, web status/QR |
| `preferences.cpp` + `.rc` | Tools → Lyrics AI Translator pages |
| `plugin_config.cpp` | load/save JSON |
| `worker_launcher.cpp` | start worker |
| `web_server_launcher.cpp` | start/stop `lyrics_server.exe`, restart on port/token change |
| `web_state_publisher.cpp` | publish state to localhost |
| `git_sync.cpp` | pull/push |
| `cache_reader.cpp` | parse cache JSON |
| `mainmenu.cpp` | View → Open / Preferences… |

Preferences GUIDs: `guids.h`.

---

## Debugging

- **View → Console** in foobar — messages `Lyrics …` / `Lyrics Web: …`
- File logging: Preferences → Advanced → Tools → Console/logging
- Worker exit codes (see also `worker/README.md`):

  | Code | Meaning |
  |------|---------|
  | 0 | OK / already ready |
  | 10 | proxy enabled but invalid (bad host/port/type) |
  | 11 | network |
  | 12 | lrclib miss |
  | 20–21 | LLM |
  | 30 | cache write |

- Web info/QR over loopback: `GET http://127.0.0.1:<port>/api/info`, `/api/qr.png` (localhost only)

---

## Limitations / known behaviour

- **x86 and x64** packages are separate; bitness must match foobar2000
- Web remote is tied to the lyrics window lifecycle (not a permanent background service by design)
- An auth token in the URL ends up in the phone browser history — usually acceptable on a home LAN
- Changing `cacheDir` does not migrate files
- SDK and WTL are not part of the public repository — only documented in this DEV guide

---

## End-user docs

See [USER_EN.md](USER_EN.md) and the root [README_EN.md](../README_EN.md).
