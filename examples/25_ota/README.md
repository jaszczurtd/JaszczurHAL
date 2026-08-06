# 25 - Native RP OTA

This example enables the native Pico SDK OTA path on Pico W and Pico 2 W. Set
the WiFi credentials and replace the development OTA password in `app.c`.
Keep the same hostname, port, and password in
`.vscode/jaszczurhal.project.json`.
The example also fixes the host callback listener at TCP port `8266`, so one
narrow firewall rule is sufficient on hosts that filter inbound callbacks.
`runmefirst.sh` detects the local IPv4 network and offers to provision that
rule persistently after showing its exact scope.

Read [Native RP OTA Workflow](../../doc/OTAWorkflow.md) for the complete
project, firmware, first-flash, VS Code, firewall, confirmation, rollback, and
recovery contract.

The application confirms a trial image only after WiFi connectivity has been
established, then starts the authenticated OTA service. Use:

```bash
../../vscode/entry/jh-vscode ota-discover --project "$PWD"
../../vscode/entry/jh-vscode upload-ota --project "$PWD" --interactive
```

For real projects, configure `ota.passwordEnv` instead of storing a password
in the manifest. The application still owns the corresponding device-side
password.
