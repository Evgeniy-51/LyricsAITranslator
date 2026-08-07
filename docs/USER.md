# Руководство пользователя

[English version](USER_EN.md)

Плагин **Lyrics AI Translator** для foobar2000 v2 (**32-bit или 64-bit** — версия пакета должен совпадать с foobar).

## Что умеет

- Показывает текст текущего трека в отдельном окне
- Подсвечивает текущую строку, если в кэше есть синхронизированные тайминги (LRC/sync)
- Получение текста из lrclib.net и перевод через LLM (OpenAI-совместимый API)
- Хранит результаты в локальной папке кэша (JSON)
- Опционально синхронизирует кэш с Git-репозиторием (GitHub/GitLab и т.п.)
- Опционально открывает веб-страницу в LAN для сетевого устройства: текст, QR, кнопки плеера

Пока окно lyrics **закрыто**, плагин не слушает playback и не запускает worker/web.

---

## Установка

### Из `.fb2k-component` (рекомендуется)

Выберите файл под разрядность foobar2000:

| foobar2000 | Файл |
|------------|------|
| 32-bit (x86) | `foo_lyrics_ai_translator_win32.fb2k-component` |
| 64-bit (x64) | `foo_lyrics_ai_translator_win64.fb2k-component` |

Внутри пакета уже есть DLL, `lyrics_worker.exe`, `lyrics_server.exe` и `config.example.json`.

1. **File → Preferences → Components → Install…**
2. Укажите нужный `.fb2k-component`.
3. Полностью перезапустите foobar2000.
4. Проверьте, что компонент в списке **Preferences → Components**.
5. Настройки: через **Preferences → Tools → Lyrics AI Translator**  
   (плагин сам создаст/обновит `config.json` рядом с DLL)  
   **или** скопируйте `config.example.json` → `config.json` в папке компонента и отредактируйте вручную.

Папка компонента обычно:

- обычная установка: `%APPDATA%\foobar2000-v2\user-components\…`
- portable / x64: свой `profile\user-components` / `user-components-x64` рядом с foobar

Кнопка **Open plugin folder** на вкладках Preferences открывает эту папку.

### Вручную (папка)

Если ставите из `dist\x86\lyrics-ai-translator\` или `dist\x64\lyrics-ai-translator\` без Install…:

1. Скопируйте **всю** папку в `user-components` вашей установки (имя папки любое, DLL внутри — `foo_lyrics_ai_translator.dll`, **не переименовывать**).
2. Настройте через Preferences или `config.example.json` → `config.json`.
3. Полностью перезапустите foobar2000.

Состав папки:

| Файл | Назначение |
|------|------------|
| `foo_lyrics_ai_translator.dll` | плагин |
| `lyrics_worker.exe` | получение/перевод текста |
| `lyrics_server.exe` | веб-UI (Web remote) |
| `config.example.json` | образец настроек |

### Важно

- Разрядность пакета = разрядность foobar2000 (**не смешивать** x86/x64).
- Не переименовывайте DLL.
- Секреты (API key, PAT, пароли) хранятся в `config.json`.

---

## Первая настройка

Откройте настройки любым способом:

- **View → Lyrics AI Translator → Preferences…**
- или **File → Preferences → Tools → Lyrics AI Translator**

Вкладки:

### Language

- **Enable translation** — включать ли перевод
- **Target language** — язык перевода (есть список + свой вариант)

### LLM

- **Base URL** — например `https://api.openai.com/v1`
- **Model** — модель API
- **API key** — ключ провайдера (**обязателен** для запроса текста/перевода)

### Proxy

Опциональный прокси для запросов worker (**и LRCLib, и LLM** идут через один клиент). **По умолчанию включён**, тип **SOCKS5**.

В Preferences: **Enable proxy**, затем **Type** — выберите **SOCKS5** или **HTTP**.

- **SOCKS5** — типичный VPN/локальный SOCKS
- **HTTP** — HTTP CONNECT (то, что часто называют «HTTP/HTTPS proxy»)
- Host / Port / User / Password
- Снимите Enable proxy, если хотите подключаться напрямую

В `config.json`:

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

`"type": "http"` — для HTTP CONNECT. Если `type` нет — SOCKS5 (если в `url` схема `http://` / `https://`, worker может вывести HTTP).

### Cache

- **Cache folder** — папка для сохранения текстов и переводов (`temp` рядом с плагином по умолчанию). Смена пути **не переносит** уже скачанные файлы.
- **Enable Git sync** — позволяет пользоваться библиотекой текстов на разных устройствах, используя гит-репозиторий как удаленное хранилище:
  - Remote URL репозитория
  - PAT (токен GitHub/GitLab)
  - Branch
  - Pull on startup / Auto setup

Без этой опции плагин просто читает/пишет локальную папку.

### Web

- **Enable web remote control** — LAN-страница для телефона
- **Port** — по умолчанию `8765`
- **Auth token (optional)**:
  - **пусто** — открыть может любой в вашей Wi‑Fi сети, кто знает адрес
  - **заполнен** (кнопка Generate) — доступ только по URL с `?token=...` (такой URL/QR показывает окно lyrics)

Web работает, пока открыто окно lyrics и рядом с DLL есть `lyrics_server.exe`.

Кнопка **Open plugin folder** на вкладках открывает каталог с DLL и `config.json`.

---

## Как пользоваться

### Окно lyrics

**View → Lyrics AI Translator → Open lyrics window**

- При смене трека плагин ищет JSON в кэше.
- Если готового текста нет — запускается `lyrics_worker.exe` (нужны LLM-настройки, сеть/прокси).
- В статусной строке окна: состояние загрузки, при включённом Web — URL и подсказка кликнуть для QR.

### Пакетная загрузка

В окне lyrics доступна кнопка **Get all lyrics** (превентивно получает тексты всех треков в плейлисте): пропускает уже готовые, без album, дубликаты; **Cancel** прерывает.

### Остановка синхронизации

Кнопка Sync в правом верхнем углу останавливает синхронизацию, при этом можете перематывать текст песни.

### Мобильное устройство (Web)

1. Включите Web в Preferences, Apply.
2. Откройте окно lyrics.
3. Клик по статусу Web → QR / URL.
4. Откройте ссылку на устройстве (устройство должно быть подключено к этой же сети).
5. При клике на ссылку открывается QR для перехода на данный URL.
6. На странице: текст, подсветка, кнопки prev / play-pause / next, ±10 с.

Если задан auth token — сканируйте именно QR из окна (в нём уже `?token=`). Голый `http://IP:PORT/` без токена вернёт unauthorized.

### Git sync

Если включён: при старте (с задержкой) может сделать pull; при появлении новых lyrics — push в remote. Нужны `git` в PATH, remote URL и PAT с правами на репозиторий.

---

## Типичные проблемы

| Симптом | Что проверить |
|---------|----------------|
| Компонента нет в списке | Разрядность foobar = пакет? Имя DLL точно `foo_lyrics_ai_translator.dll`? Перезапуск? |
| Нет текста / ошибка LLM | API key, base URL, model; proxy type/host; Console foobar |
| Worker не стартует | Есть ли `lyrics_worker.exe` рядом с DLL? |
| Web / QR не работает | Web enabled? Окно lyrics открыто? Есть `lyrics_server.exe`? Firewall на порт? Одна Wi‑Fi сеть? |
| Unauthorized в браузере | Токен задан — откройте URL с `?token=` из статуса/QR |
| Sync не тянет/не пушит | `git` установлен? remote/PAT/branch? Папка кэша = repo dir после Apply |

Логи: **View → Console** в foobar2000. При необходимости включите file logging в Advanced.

---

## Где лежат данные

- Настройки: `config.json` **рядом с DLL** (папка компонента)
- Кэш lyrics: папка из **Cache folder** (по умолчанию `temp` внутри/рядом с установкой плагина)

Пример структуры кэша: `<cacheDir>/<artist>/<album>/<title>.json`
