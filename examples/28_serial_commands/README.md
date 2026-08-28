# 28 - Serial command router

Minimal framed Serial Session application using an independent
`hal_command_router`. It registers `echo` and `info`, restricts both routes to
`HAL_COMMAND_SOURCE_SERIAL_SESSION`, and attaches `hal_serial_commands` to the
target's default serial endpoint.

This project is separate from the GPS/UART example because it needs no external
receiver or application-owned UART. On RP boards it can be exercised through
USB CDC; other targets use their selected `hal_serial` endpoint.

## Build

```bash
./scripts/examples_dispatcher.py build \
  --target rp2040 --example 28_serial_commands
./scripts/examples_dispatcher.py build \
  --target stm32g474 --example 28_serial_commands
```

The project also supports the generated RP2350 ARM and RISC-V configurations.
Build one configuration through the VS Code entrypoint with:

```bash
vscode/entry/jh-vscode build \
  --project examples/28_serial_commands --target rp2040 --board pico
```

## Serial exchange

Open the target's serial endpoint and send each request with a trailing newline.
The CRC covers the bytes between `$` and `*`, as described by
`hal_serial_frame.h`.

Activate the session first:

```text
$SC,1,HELLO*0F
```

The reply contains the module, protocol, generated session identifier, firmware
version, build identifier and device UID. The response keeps sequence `1`.

Dispatch an echo through the router:

```text
$SC,2,echo hello router*5B
$SC,2,hello router*08
```

Read request metadata and target uptime:

```text
$SC,3,info*74
```

The dynamic response has this payload shape:

```text
source=SERIAL_SESSION request=3 session=<id> uptime_ms=<value>
```

End the session with:

```text
$SC,4,BYE*EF
$SC,4,OK BYE*9B
```

Requests sent before `HELLO` receive `ERR HELLO_REQUIRED`. Unknown route names
reach the router and are returned as `ERR HAL_ENOENT` by the adapter.

## What the example shows

- creating and retaining an independent router;
- registering copied route definitions without replacing an existing name;
- restricting handlers to Serial Session requests;
- attaching caller-owned session and adapter state;
- returning text bodies with the request sequence preserved;
- reading transport-neutral request metadata inside a handler;
- releasing the adapter before destroying its router on startup rollback.
