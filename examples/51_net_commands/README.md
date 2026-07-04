# 51_net_commands

Demonstrates `HAL_ENABLE_NET_COMMANDS`: a shared command registry invoked from
both HTTP and WebSocket transports.

- HTTP: `POST /api/command` with either `status` text or
  `{"cmd":"status"}` JSON.
- WebSocket: connect to `ws://<device-ip>:81/ws` and send the same text/JSON
  payloads.
- The same `status` and `echo` callbacks handle both transports and reply with
  JSON when the request was JSON.

Set `WIFI_SSID` and `WIFI_PASSWORD` in `app.c`, flash to a WiFi-capable RP2040
board and open the printed `http://<device-ip>/` URL.
