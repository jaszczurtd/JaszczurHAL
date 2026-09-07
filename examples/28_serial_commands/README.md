<a id="28---serial-command-router"></a>

# 28 - Text commands over a serial port

This example provides two commands: `echo` returns the supplied text, and
`info` reports request metadata and device uptime. Messages use Serial
Session framing. `hal_serial_commands` receives them from the default
`hal_serial` endpoint and passes them to a separate `hal_command_router`.

Both commands accept only `HAL_COMMAND_SOURCE_SERIAL_SESSION`. This
restricts the request's transport, not the client's identity: the definitions
set `required_security = 0u`.

The project needs neither a GPS receiver nor an additional application-owned
UART. RP boards can use USB CDC; other targets use their selected
`hal_serial` endpoint.

## Build

Run from the repository root:

```bash
./scripts/examples_dispatcher.py build \
  --target rp2040 --example 28_serial_commands
./scripts/examples_dispatcher.py build \
  --target stm32g474 --example 28_serial_commands
```

RP2350 ARM and RISC-V configurations are also available. To build a single
configuration, use:

```bash
vscode/entry/jh-vscode build \
  --project examples/28_serial_commands --target rp2040 --board pico
```

## Serial exchange

Open the device's serial port and end each request with a newline.
The CRC covers bytes between `$` and `*`, as specified in
`hal_serial_frame.h`.

Start a session:

```text
$SC,1,HELLO*0F
```

The response keeps sequence number `1` and includes the module, protocol,
generated session identifier, firmware version, build identifier, and
device UID.

Send `echo`; the second line below is the response:

```text
$SC,2,echo hello router*5B
$SC,2,hello router*08
```

Request metadata and device uptime with:

```text
$SC,3,info*74
```

The response body has this form:

```text
source=SERIAL_SESSION request=3 session=<id> uptime_ms=<value>
```

End the session with `BYE`; the second line is the response:

```text
$SC,4,BYE*EF
$SC,4,OK BYE*9B
```

Requests sent before `HELLO` receive `ERR HELLO_REQUIRED`. An unknown
command reaches the router, and the adapter returns `ERR HAL_ENOENT`.
`HELLO` starts the protocol exchange but does not by itself establish the
client's identity.

## What the example shows

The application creates its own router and registers commands without
replacing existing names. The router copies command definitions; the
application retains ownership of the session and adapter objects.

Handlers read transport-independent metadata and return text with the
request's sequence number. If initialization fails, the application releases
the adapter before destroying the router it uses.
