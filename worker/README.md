# lyrics_worker

Go worker для lrclib + LLM-перевода. Пока настройки и трек — из `config.json`.

Полная документация сборки и архитектуры: [../docs/DEV.md](../docs/DEV.md).

## Сборка (32-bit для foobar2000)

```powershell
cd APP\worker
$env:GOOS="windows"
$env:GOARCH="386"
$env:CGO_ENABLED="0"
go build -ldflags="-s -w" -o ..\lyrics_worker.exe .\cmd\lyrics_worker
```

## Конфиг

Скопируй `APP\config.example.json` → `APP\config.json`. Обязательны `track.artist`, `track.title`, `track.album` (путь кэша: `temp/<artist>/<album>/<title>.json`).

Запуск из папки `APP`:

```powershell
.\lyrics_worker.exe -config config.json
```

Кэш по умолчанию: `APP\temp` (папка рядом с config; в примере `"cacheDir": "temp"`).
`album` и `durationSec` в track опциональны — lrclib ищет и без них.

**Proxy (опционально, в example `enabled: true`):** `type`: `socks5` (default) или `http` (HTTP CONNECT). Host в `url`, порт в `port` (или `host:port` / `socks5://…` / `http://…` в url). LRCLib и LLM используют один клиент. `enabled: false` → direct. SOCKS4 не поддерживается.

## Exit codes

| Code | Значение |
|------|----------|
| 0 | OK / кэш уже ready |
| 10 | proxy включён, но невалиден |
| 11 | сеть |
| 12 | lrclib не нашёл трек |
| 20 | LLM ошибка |
| 21 | LLM невалидный JSON |
| 30 | ошибка записи кэша |
