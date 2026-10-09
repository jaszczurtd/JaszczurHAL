"""HAL renderers: what the current driver APIs accept from a device tree.

A renderer belongs to one HAL configuration type. It rejects settings its
API cannot apply instead of exporting a macro the driver would ignore, and
an endpoint the destination field would read as a sentinel. When a node is
complete, it writes a deferred initializer: designated fields in C11, and in
C++17 positional fields or, for a union member other than the first, a pure
immediately invoked lambda.
"""

from __future__ import annotations

from typing import Any, Callable

from hardware_literals import c_literal, macro_token

# SPI settings fixed by a driver: (frequency or None for any, mode, bit order).
SPI_FIXED = {
    "mcp2515": (10000000, 0, "msb"),
    "mcp251xfd": (None, 0, "msb"),
    "sx126x": (None, 0, "msb"),
    "ili9341": (None, 0, "msb"),
}
# Configuration type of each renderer and its SPI clock field, if any.
RENDERER_TYPES = {
    "mcp2515": ("hal_can_mcp2515_config_t", None),
    "mcp251xfd": ("hal_can_mcp251xfd_config_t", "spi_clock_hz"),
    "sx126x": ("hal_lora_radio_config_t", "spi_clock_hz"),
    "ili9341": ("hal_display_ili9341_config_t", "clock_hz"),
    "fdcan": ("hal_can_stm32g474_fdcan_config_t", None),
}
# Bus lines each driver uses; a bus without one of them leaves the node
# without an initializer. The ILI9341 driver only writes to the panel.
BUS_LINES = {"mcp2515": ("sck", "mosi", "miso"), "mcp251xfd": ("sck", "mosi", "miso"),
             "sx126x": ("sck", "mosi", "miso"), "ili9341": ("sck", "mosi")}
RENDERERS = {"microchip,mcp2515": "mcp2515", "semtech,sx1262": "sx126x",
             "ilitek,ili9341": "ili9341", "jaszczurhal,can-transceiver": "fdcan",
             "microchip,mcp2562fd": "fdcan"}
# HAL fields that read 0 as "use the default pin" (STM32G474 FDCAN).
DEFAULT_PIN_FIELDS = {"fdcan": {"rx", "tx"}}
FDCAN_ARBITRATION_HZ = 500000
FDCAN_DATA_HZ = 2000000
PARITY = {"none": (0, "N"), "even": (1, "E"), "odd": (2, "O")}


def transport_problem_for(renderer: str, transport: dict[str, Any]) -> tuple[str, str] | None:
    """The first transport setting the renderer's API cannot apply."""
    fixed = SPI_FIXED.get(renderer)
    if fixed is None:
        return None
    frequency, mode, order = fixed
    if frequency is not None and transport.get("frequencyHz") != frequency:
        return "frequencyHz", f"the {renderer} driver runs SPI at {frequency} Hz only"
    if transport.get("mode", 0) != mode:
        return "mode", f"the {renderer} driver uses SPI mode {mode} only"
    if transport.get("bitOrder", "msb") != order:
        return "bitOrder", f"the {renderer} driver sends {order.upper()} first only"
    return None


def transport_problem(compatible: str, transport: dict[str, Any],
                      properties: dict[str, Any]) -> tuple[str, str] | None:
    renderer = RENDERERS.get(compatible)
    if renderer is None:
        return None
    problem = transport_problem_for(renderer, transport)
    limit = properties.get("maxSpiClockHz")
    if problem is None and renderer == "sx126x" and limit is not None and transport["frequencyHz"] > limit:
        problem = "frequencyHz", f"the radio takes at most maxSpiClockHz = {limit} Hz"
    return problem


def endpoint_problem(compatible: str, signal: str, encoded: int | None) -> str | None:
    """A pin the destination field would read as its default."""
    if encoded == 0 and signal in DEFAULT_PIN_FIELDS.get(RENDERERS.get(compatible, ""), ()):
        return f"the HAL reads {signal} pin 0 as the default pin of the controller"
    return None


def uart_frame_problem(target: str, frame: dict[str, Any]) -> tuple[str, str] | None:
    """Frames the target UART adapter rejects, as (pointer, message)."""
    if target == "stm32g474":
        if frame["dataBits"] == 5:
            return "/frame/dataBits", "the STM32G474 USART has no 5-bit frames"
        if frame["dataBits"] == 6 and frame["parity"] == "none":
            return "/frame/dataBits", "the STM32G474 USART needs parity with 6 data bits"
    return None


def uart_frame_token(frame: dict[str, Any]) -> str:
    return f"HAL_UART_CFG_{frame['dataBits']}{PARITY[frame['parity']][1]}{frame['stopBits']}"


def spi_settings_init(transport: dict[str, Any]) -> tuple[str, str]:
    """hal_spi_settings_t: clock_hz, bit_order, data_mode, in this order."""
    clock = c_literal("uint32", transport["frequencyHz"])
    order = "1U" if transport["bitOrder"] == "msb" else "0U"
    mode = c_literal("uint8", transport["mode"])
    return (f"{{.clock_hz = {clock}, .bit_order = {order}, .data_mode = {mode}}}",
            f"{{{clock}, {order}, {mode}}}")


class Fields:
    """One structure written as C designated fields and C++ positions."""

    def __init__(self) -> None:
        self.items: list[tuple[str, str, str]] = []

    def add(self, name: str, c_value: str, cpp_value: str | None = None) -> "Fields":
        self.items.append((name, c_value, cpp_value if cpp_value is not None else c_value))
        return self

    def c(self) -> str:
        return "{" + ", ".join(f".{name} = {value}" for name, value, _ in self.items) + "}"

    def cpp(self) -> str:
        return "{" + ", ".join(value for _, _, value in self.items) + "}"

    def assignments(self, prefix: str) -> str:
        return " ".join(f"{prefix}.{name} = {value};" for name, _, value in self.items)


def boolean(value: bool) -> tuple[str, str]:
    return ("1" if value else "0"), ("true" if value else "false")


class NodeView:
    """What a renderer reads from one resolved node."""

    def __init__(self, path: str, node: dict[str, Any], hardware: dict[str, Any]) -> None:
        self.path, self.node, self.hardware = path, node, hardware
        self.properties = {name: item["value"] for name, item in node.get("properties", {}).items()}
        bus = node.get("bus")
        self.bus = hardware["buses"].get(bus) if isinstance(bus, str) else bus
        self.transport = node.get("transport", {})

    def pin(self, signal: str) -> int | None:
        endpoint = self.node["signals"].get(signal, {}).get("endpoint")
        return encode_pin(endpoint)

    def bus_pin(self, name: str) -> int | None:
        return encode_pin(self.bus["pins"].get(name)) if self.bus else None

    def owns(self, signal: str) -> bool:
        item = self.node["signals"].get(signal, {})
        return item.get("endpoint") is None or item.get("owner") == f"{self.path}.{signal}"


def encode_pin(endpoint: dict[str, Any] | None) -> int | None:
    """HAL encoding of a SoC endpoint (STM32: port * 16 + pin) or a board
    component pin that advertises one."""
    if endpoint is None:
        return None
    if endpoint["domain"] == "soc-gpio":
        pin = endpoint["id"]
        return (ord(pin[1]) - ord("A")) * 16 + int(pin[2:]) if isinstance(pin, str) else pin
    return endpoint.get("halPin")


def render_mcp2515(view: NodeView) -> tuple[str, str] | None:
    cs = view.pin("cs")
    if cs is None:
        return None
    one_shot, wakeup = boolean(True), boolean(True)   # as hal_can_default_config()
    inner = (Fields().add("spi_bus", c_literal("uint8", view.bus["index"]))
             .add("cs_pin", c_literal("uint8", cs))
             .add("bitrate_hz", c_literal("uint32", view.properties["bitrateHz"]))
             .add("oscillator_hz", c_literal("uint32", view.properties["oscillatorHz"]))
             .add("one_shot_tx", *one_shot).add("sleep_wakeup", *wakeup))
    return (f"{{.backend = HAL_CAN_BACKEND_MCP2515, .mcp2515 = {inner.c()}}}",
            f"{{HAL_CAN_BACKEND_MCP2515, {{{inner.cpp()}}}}}")


def render_fdcan(view: NodeView) -> tuple[str, str] | None:
    """The values of hal_can_board_config() for one channel."""
    controller = view.node.get("controller")
    rx, tx = view.pin("rx"), view.pin("tx")
    if controller is None or rx is None or tx is None:
        return None
    standby = view.pin("standby")
    limit = view.properties["maxBitrateHz"]
    fields = (Fields().add("instance", c_literal("uint8", controller["index"] + 1))
              .add("rx_pin", c_literal("uint8", rx)).add("tx_pin", c_literal("uint8", tx))
              .add("has_standby", *boolean(standby is not None))
              .add("standby_pin", c_literal("uint8", standby if standby is not None else 0))
              .add("standby_high", *boolean(view.properties["standbyActiveHigh"]))
              .add("arbitration_bitrate_hz", c_literal("uint32", FDCAN_ARBITRATION_HZ))
              .add("data_bitrate_hz", c_literal("uint32", min(limit, FDCAN_DATA_HZ)))
              .add("transceiver_max_bitrate_hz", c_literal("uint32", limit))
              .add("enable_fd", *boolean(True)).add("one_shot_tx", *boolean(False)))
    return (f"{{.backend = HAL_CAN_BACKEND_STM32G474_FDCAN, .stm32g474_fdcan = {fields.c()}}}",
            "([]() { hal_can_config_t config{}; "
            "config.backend = HAL_CAN_BACKEND_STM32G474_FDCAN; "
            f"{fields.assignments('config.stm32g474_fdcan')} return config; }}())")


def render_sx126x(view: NodeView) -> tuple[str, str] | None:
    p = view.properties
    pins = {name: view.pin(name) for name in ("cs", "reset", "busy", "dio1", "rfSwitchA", "rfSwitchB")}
    bus = {name: view.bus_pin(name) for name in ("miso", "mosi", "sck")}
    mode = p["rfSwitchMode"]
    wired = (pins["rfSwitchA"] is not None, pins["rfSwitchB"] is not None)
    expected = {"none": (False, False), "dio2": (False, False), "single-gpio": (True, False),
                "dio2-single-gpio": (True, False), "dual-gpio": (True, True)}[mode]
    if (None in [pins[n] for n in ("cs", "reset", "busy", "dio1")]
            or wired != expected or p.get("maxSpiClockHz") is None
            or (p["tcxoControl"] == "dio3" and (p.get("tcxoStartupUs") or 0) == 0)):
        return None
    none = "HAL_LORA_PIN_NONE"
    pin = lambda value: c_literal("uint8", value) if value is not None else none
    hardware = (Fields().add("reset_pin", pin(pins["reset"])).add("dio1_pin", pin(pins["dio1"]))
                .add("busy_pin", pin(pins["busy"]))
                .add("rf_switch_mode", f"HAL_LORA_RF_SWITCH_{macro_token(mode)}")
                .add("rf_switch_pin_a", pin(pins["rfSwitchA"])).add("rf_switch_pin_b", pin(pins["rfSwitchB"])))
    for field_name in ("IdleLevelA", "IdleLevelB", "RxLevelA", "RxLevelB", "TxLevelA", "TxLevelB"):
        hardware.add(f"rf_switch_{macro_token(field_name).lower()}", *boolean(p[f"rfSwitch{field_name}"]))
    hardware.add("regulator_mode", f"HAL_LORA_REGULATOR_{macro_token(p['regulator'])}")
    hardware.add("tcxo_control", f"HAL_LORA_TCXO_CONTROL_{macro_token(p['tcxoControl'])}")
    hardware.add("tcxo_voltage", f"HAL_LORA_TCXO_{macro_token(p.get('tcxoVoltage', '1v6'))}")
    for field_name, key, c_type in (("tcxo_startup_us", "tcxoStartupUs", "uint32"),
                                    ("min_frequency_hz", "minFrequencyHz", "uint32"),
                                    ("max_frequency_hz", "maxFrequencyHz", "uint32"),
                                    ("max_spi_clock_hz", "maxSpiClockHz", "uint32"),
                                    ("min_tx_power_dbm", "minTxPowerDbm", "int8"),
                                    ("max_tx_power_dbm", "maxTxPowerDbm", "int8")):
        hardware.add(field_name, c_literal(c_type, p.get(key, 0)))
    outer = (Fields().add("model", "HAL_LORA_RADIO_SX1262")
             .add("spi_bus", c_literal("uint8", view.bus["index"]))
             .add("spi_miso_pin", pin(bus["miso"])).add("spi_mosi_pin", pin(bus["mosi"]))
             .add("spi_sck_pin", pin(bus["sck"])).add("cs_pin", pin(pins["cs"]))
             .add("spi_clock_hz", c_literal("uint32", view.transport["frequencyHz"])))
    return (outer.c()[:-1] + f", .hardware = {{.sx126x = {hardware.c()}}}}}",
            outer.cpp()[:-1] + f", {{{hardware.cpp()}}}}}")


def render_ili9341(view: NodeView) -> tuple[str, str] | None:
    dc = view.pin("dc")
    if dc is None:
        return None
    pin = lambda value: c_literal("int16", value if value is not None else -1)
    fields = (Fields().add("bus", c_literal("uint8", view.bus["index"])).add("cs_pin", pin(view.pin("cs")))
              .add("dc_pin", pin(dc)).add("rst_pin", pin(view.pin("reset")))
              .add("clock_hz", c_literal("uint32", view.transport["frequencyHz"])))
    return fields.c(), fields.cpp()


CONFIG_RENDERERS: dict[str, tuple[Callable[[NodeView], tuple[str, str] | None], str, str]] = {
    # renderer: (function, configuration type, header and feature that declare it)
    "mcp2515": (render_mcp2515, "hal_can_config_t", "hal_can.h, HAL_ENABLE_CAN"),
    "fdcan": (render_fdcan, "hal_can_config_t", "hal_can.h, HAL_ENABLE_CAN"),
    "sx126x": (render_sx126x, "hal_lora_radio_config_t", "hal_lora_radio.h, HAL_ENABLE_LORA"),
    "ili9341": (render_ili9341, "hal_display_ili9341_config_t", "hal_display.h, HAL_ENABLE_TFT"),
}


def config_init(path: str, node: dict[str, Any], hardware: dict[str, Any]) -> tuple[str, str, str] | None:
    """(C, C++, comment) for a complete node of a supported type that owns
    the signals it drives; None leaves CONFIG_AVAILABLE at 0."""
    renderer = RENDERERS.get(node["compatible"])
    if renderer is None or not node.get("present"):
        return None
    view = NodeView(path, node, hardware)
    if not all(view.owns(name) for name in node.get("signals", {})):
        return None
    function, config_type, visibility = CONFIG_RENDERERS[renderer]
    if renderer in BUS_LINES and (view.bus is None
                                  or any(view.bus_pin(line) is None for line in BUS_LINES[renderer])):
        return None
    rendered = function(view)
    if rendered is None:
        return None
    return rendered[0], rendered[1], f"{config_type}; needs {visibility}"
