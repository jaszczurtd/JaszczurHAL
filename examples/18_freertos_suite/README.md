<a id="18---freertos-suite"></a>

# 18 - FreeRTOS tasks and network services

The base example demonstrates FreeRTOS tasks and synchronized access to
shared data. The `network` variant adds services that can be tested together
in one application.

`app.c` runs two application tasks and two additional workers. It uses HAL
and native FreeRTOS mutexes, delays, idle processing, and GPIO. This basic
runtime test is also available for RP2350 RISC-V.

The `network` variant builds `network_app.c` for WiFi-capable RP2040/RP2350
ARM boards and STM32G474 with PIM730. It provides an HTTP server, WebSocket,
RAM-backed files, commands, and a network console. Separate workers handle
BSD TCP/UDP sockets and an HTTP/HTTPS client, while two application tasks
remain active. cJSON and Telegram support are included, but **compiling
Telegram support does not send a notification**: the application contains
neither any credentials nor a send probe.

Each service has one instance. The HTTP server uses seven of the default
eight routes: `/`, `/api/status`, the HTTP command endpoint, and four RAM-file
routes. WebSocket and console messages share the `status` and `echo` handlers.

## Network configuration

Set these compile definitions before running:

| Setting | Purpose |
|---|---|
| `NETWORK_SUITE_WIFI_SSID`, `NETWORK_SUITE_WIFI_PASSWORD` | WiFi credentials. |
| `NETWORK_SUITE_REMOTE_HOST`, `NETWORK_SUITE_BSD_TCP_PORT`, `NETWORK_SUITE_BSD_UDP_PORT` | Remote servers used by the periodic BSD client probes. |
| `NETWORK_SUITE_HTTP_HOST` | Server used by the HTTP/HTTPS client. |
| `NETWORK_SUITE_CONSOLE_PASSWORD` | Password for the console on port 2323. |

The default services are HTTP on port 80, WebSocket at `ws://<ip>:81/ws`,
TCP echo on port 8080, and UDP echo on port 9000. The HTTP server and HTTPS
client are separate demonstrations; the example does not configure an HTTPS
server here.

Resource limits are 4 TCP listeners, 6 TCP sockets, a listener backlog of 2,
one TLS handle, and one client for each HTTP, WebSocket, and console service.
FreeRTOS stacks contain 1536 words for the server/application task, 384 for
the second application task, 768 for the BSD worker, and 1536 for the
HTTP/HTTPS client. These sizes are stack words, not bytes.

## Enabling the HTTPS client

By default, the client performs an HTTP request and reports that HTTPS needs
a certificate configuration. It does not bypass verification to make the
request succeed.

To run HTTPS, define `HTTP_EXAMPLE_CA_AVAILABLE` and add `ca_certificate.h`
beside the application source. Supply a trusted CA certificate in DER format
for server verification:

```c
const unsigned char http_example_ca_der[] = { /* DER CA certificate */ };
const unsigned int http_example_ca_der_len = sizeof(http_example_ca_der);
```

Before the HTTPS request, the worker waits for NTP synchronization and converts
the certificate to the HAL trust-anchor format. TLS and Telegram support are
compiled even without this file. That checks build integration, not a working
connection on the device.

The STM32G474 configuration has little spare RAM. Test the HTTPS-enabled
variant on the intended board and inspect its link map before deployment.

## Preparing the CA certificate

`ca_certificate.h` should contain trusted CA certificate used to authenticate
the server selected by `NETWORK_SUITE_HTTP_HOST`.

For a quick test, you can download the server certificate in PEM format for
the server you want to connect to. To do this, use the following command:

```bash
HTTPS_HOST=example.com

openssl s_client \
  -showcerts \
  -connect "${HTTPS_HOST}:443" \
  -servername "${HTTPS_HOST}" </dev/null 2>/dev/null |
awk '
  /BEGIN CERTIFICATE/ { count++ }
  count == 2 { print }
  /END CERTIFICATE/ && count == 2 { exit }
' > /tmp/jh-https-ca.pem
```

Verify that the selected certificate is a CA certificate. If `CA:TRUE` is not
printed, obtain the correct CA certificate from its operator instead:

```bash
openssl x509 -in /tmp/jh-https-ca.pem -noout -subject -issuer
openssl x509 -in /tmp/jh-https-ca.pem -noout -text | grep "CA:TRUE"
```

Convert the certificate to DER and generate the header expected by the
example. Run these commands from the repository root:

```bash
openssl x509 \
  -in /tmp/jh-https-ca.pem \
  -outform DER \
  -out /tmp/jh-https-ca.der

xxd -i -n http_example_ca_der /tmp/jh-https-ca.der |
sed \
  -e 's/^unsigned char /const unsigned char /' \
  -e 's/^unsigned int /const unsigned int /' \
  > examples/18_freertos_suite/ca_certificate.h
```

Then add the following definitions to `hal_project_config.h`:

```c
#define HTTP_EXAMPLE_CA_AVAILABLE 1
#define NETWORK_SUITE_HTTP_HOST "example.com"
```

The configured host must match a DNS name or IP address from the server
certificate's Subject Alternative Name. If you control the HTTPS server, you
may generate your own CA and sign the server certificate with it. The tested
OpenSSL sequence in
[`tests/run_bearssl_native_integration.sh`](../../tests/run_bearssl_native_integration.sh).
