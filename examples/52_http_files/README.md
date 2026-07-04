# 52_http_files

Demonstrates `HAL_ENABLE_HTTP_FILES`: static file serving, ETag handling and
multipart upload on top of `hal_http_server`.

This example uses a tiny RAM-backed file table so it can compile without a
portable filesystem open/read/write API. Real firmware can replace the
`ram_stat`, `ram_read` and `ram_write` callbacks with LittleFS, FatFs/SD or
flash-asset callbacks.

Set `WIFI_SSID` and `WIFI_PASSWORD` in `app.c`, flash to a WiFi-capable RP2040
board and open the printed `http://<device-ip>/` URL.
