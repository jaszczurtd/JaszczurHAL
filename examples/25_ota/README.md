<a id="25---native-rp-ota"></a>

# 25 - WiFi firmware updates on RP boards

This example enables OTA updates on Pico W and Pico 2 W using the Pico SDK
integration. Set the WiFi credentials and replace the example OTA password
in `app.c`. The hostname, port, and password must match
`.vscode/jaszczurhal.project.json`.

The computer listens for callback connections on TCP port `8266`.
A firewall that blocks inbound connections needs a rule allowing this traffic.
`runmefirst.sh` detects the local IPv4 network, shows the rule's scope, and
offers to add it persistently.

See [OTA updates on RP boards](../../doc/en/OTAWorkflow.md) for project setup,
first flashing, VS Code use, firewall configuration, update confirmation,
rollback, and BOOTSEL recovery.

The application confirms a trial image only after connecting to WiFi. It then
starts the OTA service, which requires authentication. Run from this example's
directory:

```bash
../../vscode/entry/jh-vscode ota-discover --project "$PWD"
../../vscode/entry/jh-vscode upload-ota --project "$PWD" --interactive
```

In a deployed project, set `ota.passwordEnv` so the computer-side tool reads
the password from an environment variable rather than the project file.
The application must still configure the matching device-side password.
