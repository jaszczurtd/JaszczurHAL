# HTTP And HTTPS Client Example

This example issues one plaintext HTTP request and, when a root CA is supplied,
one hostname-verified HTTPS request through BearSSL. It targets Pico W on
RP2040 and Pico 2 W on RP2350 ARM.

Set the WiFi credentials and request hostname in
`hal_project_config.h`. For HTTPS, obtain the issuing root CA for that hostname,
convert its PEM certificate to DER, and generate the header expected by
`app.cpp`:

```bash
mkdir -p .build/http-example
openssl x509 -in root-ca.pem -outform DER \
  -out .build/http-example/root-ca.der
xxd -i -n http_example_ca_der .build/http-example/root-ca.der \
  > examples/56_http_https_client/ca_certificate.h
```

Then uncomment this project-local definition in `hal_project_config.h`:

```c
#define HTTP_EXAMPLE_CA_AVAILABLE
```

Alternatively pass `HTTP_EXAMPLE_CA_AVAILABLE` as a build definition. Keep
the certificate hostname aligned with `HTTP_EXAMPLE_HOST`. The application
joins WiFi, starts NTP, waits for a plausible Unix time, and then performs the
HTTPS request with the DER trust anchor.

Build through the normal dispatcher:

```bash
vscode/entry/jh-vscode build \
  --project examples/56_http_https_client \
  --target rp2040 --board picow
```

The loopback integration gate
[`tests/run_bearssl_native_integration.sh`](../../tests/run_bearssl_native_integration.sh)
generates temporary CA/server keys and certificates with OpenSSL, verifies
valid and invalid hostname/time cases, and removes its temporary material on
exit.

The full request, response, timeout, trust, and lifecycle contract is in the
[`hal_http_client` API](../../doc/api/15_connectivity.md#halhttpclient-httphttps-client-opt-in-halenablehttpclient).
