# lyrics_server

LAN web UI for Lyrics AI Translator. Receives playback state from the foobar plugin via `POST http://127.0.0.1:<port>/api/state`.

Build / architecture: [../docs/DEV.md](../docs/DEV.md). User guide: [../docs/USER.md](../docs/USER.md).

## Build

```powershell
cd lyrics_server
$env:GOOS="windows"; $env:GOARCH="386"; $env:CGO_ENABLED="0"   # or amd64
go build -ldflags="-s -w" -o ..\dist\x86\lyrics-ai-translator\lyrics_server.exe .\cmd\lyrics_server
```

## Run standalone

```powershell
.\lyrics_server.exe -host 0.0.0.0 -port 8765
```

Open `http://<PC-LAN-IP>:8765/` from phone or browser.
