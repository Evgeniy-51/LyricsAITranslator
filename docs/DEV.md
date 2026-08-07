[English version](DEV_EN.md)

## Архитектура

```
foobar2000
  └─ foo_lyrics_ai_translator.dll
        ├─ окно lyrics / Preferences / config.json
        ├─ запускает lyrics_worker.exe   → пишет JSON в cacheDir
        └─ запускает lyrics_server.exe   → LAN UI (пока открыто окно)
              ↑ POST /api/state с ПК
              ↓ GET страница / API с телефона (+ optional ?token=)
```

| Компонент | Язык | Роль |
|-----------|------|------|
| `plugin/` | C++ (foobar SDK, WTL) | UI, playback hooks, чтение кэша, launch процессов |
| `worker/` | Go | lrclib + LLM, запись cache JSON |
| `lyrics_server/` | Go | HTTP + SSE/web UI, QR, player commands |

Конфиг один: `config.json` рядом с DLL. Preferences читают/пишут его через `plugin_config`.

Worker **не** держит долгоживущий сервис: стартует по необходимости, пишет файл, выходит.

Web-сервер живёт, пока плагин держит процесс (сейчас — пока релевантно окно lyrics / web enabled).

**Разрядность:** DLL, worker и server в одном пакете должны совпадать (x86↔386 или x64↔amd64). Не смешивать.

---

## Дерево репозитория

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
├── out\Win32|x64\              # DLL после MSBuild (не в git)
└── dist\x86|x64\lyrics-ai-translator\  # полный пакет (не в git)
```

---

## Зависимости

### Обязательные для сборки DLL

1. **foobar2000 SDK** (у проекта: `SDK-2025-03-07`)  
   Solution ссылается на SDK **на уровень выше** корня репозитория:

   ```
   parent/
     this-repo/          ← git clone (этот репозиторий)
     SDK-2025-03-07/     ← SDK рядом, не внутри git
   ```

   Пути в `lyrics-plugin.sln`: `..\SDK-2025-03-07\...`  
   Если SDK лежит иначе — поправьте пути в solution/vcxproj.

2. **WTL 10** (заголовки). По умолчанию: `C:\Tools\WTL10\Include`  
   Переопределение: env `WTLIncludeDir`.

3. **Visual Studio / Build Tools** с C++:
   - workload Desktop C++
   - toolset **v145** (VS 2026)
   - **C++ ATL** для toolset (x86 **и** x64)
   - Windows SDK

4. **Go** (для worker и lyrics_server):
   - worker: см. `worker/go.mod` (Go 1.22+)
   - server: см. `lyrics_server/go.mod`
   - `GOOS=windows` + `GOARCH=386` (пакет x86) или `amd64` (пакет x64), `CGO_ENABLED=0`

### Runtime (у пользователя)

- foobar2000 v2 **той же разрядности**, что и пакет
- `git` в PATH — только если нужен sync
- сеть / прокси (**SOCKS5** или **HTTP CONNECT**) — для worker; SOCKS4 не поддерживается

---

## Сборка

### DLL

```bat
build-release.bat              rem x86 (default)
build-release.bat x64
build-release.bat all          rem обе разрядности
build-release.bat x86 smoke
```

- MSBuild `lyrics-plugin.sln` Release|x86 или Release|x64
- DLL → `out\Win32\` или `out\x64\`
- копируется в `dist\<arch>\lyrics-ai-translator\`
- `.fb2k-component`: `dist\foo_lyrics_ai_translator_win32.fb2k-component` / `_win64`

### Полный пакет (DLL + Go)

Сначала DLL, затем:

```powershell
.\scripts\package-release.ps1              # x86
.\scripts\package-release.ps1 -Arch x64
.\scripts\package-release.ps1 -Arch all
```

Соберёт `lyrics_worker.exe`, `lyrics_server.exe`, `config.example.json`, `INSTALL.txt` в `dist\<arch>\lyrics-ai-translator\`.

### Только worker

```powershell
cd worker
$env:GOOS="windows"; $env:GOARCH="386"; $env:CGO_ENABLED="0"   # or amd64 for x64
go build -ldflags="-s -w" -o ..\dist\x86\lyrics-ai-translator\lyrics_worker.exe .\cmd\lyrics_worker
```

Ручной прогон:

```powershell
.\lyrics_worker.exe -config path\to\config.json
```

В конфиге для ручного запуска нужны поля `track.artist` / `title` / (желательно) `album`.

### Только lyrics_server

```powershell
cd lyrics_server
$env:GOOS="windows"; $env:GOARCH="386"; $env:CGO_ENABLED="0"   # or amd64
go build -ldflags="-s -w" -o ..\dist\x86\lyrics-ai-translator\lyrics_server.exe .\cmd\lyrics_server
```

```powershell
.\lyrics_server.exe -host 0.0.0.0 -port 8765
# опционально: -token secret
```

### Smoke-сборка DLL

```bat
build-release.bat x86 smoke
build-release.bat x64 smoke
```

---

## Конфигурация

Источник правды для полей: `config.example.json` + `plugin_config.cpp` / Preferences.

Основные блоки:

| Блок | Назначение |
|------|------------|
| `llm` | baseUrl, model, apiKey |
| `proxy` | `enabled`, `type` (`socks5`\|`http`), `url`/`port`/`user`/`password`; оба LRCLib+LLM через один `httpclient`; `enabled: false` = direct; без `type` → socks5 (или http, если в url схема http/https). SOCKS4 нет |
| `targetLang` / `enableTranslation` | язык перевода |
| `cacheDir` | корень JSON-кэша |
| `sync` | Git remote, PAT, branch, pullOnStartup, autoSetup; `repoDir` при Apply из Preferences = cacheDir |
| `web` | enabled, host, port, authToken (пустой = без защиты), updateIntervalMs |

Пустой `web.authToken`: аутентификация отключена — `/`, `/api/state` (GET) и SSE доступны без токена.  
Непустой: браузерные эндпоинты принимают токен через query `?token=…` или cookie `lyrics_token`. Loopback API плагина (`POST /api/state`, poll highlight/player) auth не требует.

---

## Важные модули плагина

| Файл | Содержание |
|------|------------|
| `lyrics_window.cpp` | UI окна, кэш, worker queue, web status/QR |
| `preferences.cpp` + `.rc` | страницы Tools → Lyrics AI Translator |
| `plugin_config.cpp` | load/save JSON |
| `worker_launcher.cpp` | старт worker |
| `web_server_launcher.cpp` | старт/стоп `lyrics_server.exe`, рестарт при смене port/token |
| `web_state_publisher.cpp` | публикация state на localhost |
| `git_sync.cpp` | pull/push |
| `cache_reader.cpp` | разбор JSON кэша |
| `mainmenu.cpp` | View → Open / Preferences… |

GUID’ы Preferences: `guids.h`.

---

## Отладка

- **View → Console** в foobar — сообщения `Lyrics …` / `Lyrics Web: …`
- File logging: Preferences → Advanced → Tools → Console/logging
- Worker exit codes (см. также `worker/README.md`):

  | Code | Смысл |
  |------|--------|
  | 0 | OK / уже ready |
  | 10 | proxy enabled but invalid (bad host/port/type) |
  | 11 | сеть |
  | 12 | lrclib miss |
  | 20–21 | LLM |
  | 30 | запись кэша |

- Web info/QR с loopback: `GET http://127.0.0.1:<port>/api/info`, `/api/qr.png` (только с localhost)

---

## Ограничения / известное поведение

- Пакеты **x86 и x64** раздельные; разрядность должна совпадать с foobar2000
- Web remote завязан на жизненный цикл окна lyrics (не «всегда в фоне», пока так задумано)
- Auth token в URL попадает в историю браузера телефона — для домашней LAN обычно приемлемо
- Смена `cacheDir` не мигрирует файлы
- SDK и WTL в публичный репозиторий не входят — только в DEV-инструкции

---

## Документы для пользователя

См. [USER.md](USER.md) / [USER_EN.md](USER_EN.md) и корневые [README.md](../README.md) / [README_EN.md](../README_EN.md).
