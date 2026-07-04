# 50_net_console

Password-protected TCP mirror for `hal_serial` / `deb` / `derr` output.

Logs still go to UART/USB. After WiFi connects, authenticated TCP clients also
receive a copy on port `2323`, and lines typed by the client are delivered to
firmware command callbacks.

Before flashing, change `WIFI_SSID`, `WIFI_PASSWORD` and `CONSOLE_PASSWORD` in
`app.c`. The password protects access to this console, but the transport is
plain TCP; use it only on a trusted network or behind a secure tunnel.
