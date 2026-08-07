# Lyrics AI Translator

Плагин для **foobar2000 v2** (**32-bit и 64-bit**): позволяет автоматически получить текст песни, перевод на язык пользователя, выводить текст с подстрочным переводом в окно плагина либо на другое устройство в локальной сети (ТВ, телефон, планшет). Также возможно с удаленного устройства управлять проигрыванием (останавливать либо "перематывать" трек). При повторном проигрывании текст и перевод загружаются из кэша. 


## Возможности

- Окно lyrics с подсветкой синхронизированных строк (если есть тайминги)
- Получение текста и перевод через OpenAI-совместимый LLM API
- Маршрутизация запросов к LLM и LRCLib через прокси (**SOCKS5** или **HTTP CONNECT**; опционально, в example по умолчанию включён)
- Локальный JSON-кэш; опционально Git sync (GitHub/GitLab + PAT)
- Web remote: страница на телефоне, QR-код, управление плеером
- Настройки в Preferences foobar2000 (Language / LLM / Proxy / Cache / Web)

## Быстрый старт (пользователь)

1. Трбуется установленный **foobar2000 v2** (x86 или x64 — пакет должен совпадать).
2. **Preferences → Components → Install…** → `foo_lyrics_ai_translator_win32.fb2k-component` или `_win64` (см. [docs/USER.md](docs/USER.md)).
3. Перезапустите foobar; укажите API key LLM в Preferences (или через `config.json`).
4. **View → Lyrics AI Translator → Open lyrics window**.

Подробно: **[docs/USER.md](docs/USER.md)**.

## Разработчикам

Сборка DLL + Go worker/server, зависимости (SDK, WTL, VS, Go), архитектура:

**[docs/DEV.md](docs/DEV.md)**

Кратко: нужен foobar2000 SDK рядом с этим репозиторием (`../SDK-2025-03-07/`), WTL 10, VS Build Tools (x86+x64), Go. Затем `build-release.bat all` и `.\scripts\package-release.ps1 -Arch all`.

## Структура репозитория

| Путь | Назначение |
|------|------------|
| `plugin/` | C++ компонент foobar2000 (`foo_lyrics_ai_translator.dll`) |
| `worker/` | Go: lrclib + LLM → JSON в кэше |
| `lyrics_server/` | Go: LAN web UI |
| `config.example.json` | Пример настроек (без секретов) |
| `docs/` | Документация |

## Требования

- foobar2000 **v2.x** — пакет **той же разрядности** (x86 или x64)
- Для перевода: доступ к OpenAI-совместимому API (и при необходимости SOCKS5 / HTTP-прокси)
- Для Git sync: установленный `git`
- Для web: `lyrics_server.exe` рядом с DLL; окно lyrics должно быть открыто

## Лицензия / дисклеймер

API-ключи, PAT и пароли хранятся локально в `config.json` — **не публикуйте** этот файл. LLM и сторонние сервисы могут тарифицироваться отдельно.
