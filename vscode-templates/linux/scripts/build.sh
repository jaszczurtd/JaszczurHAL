#!/usr/bin/env bash
# =============================================================================
# JaszczurHAL build wrapper for VS Code tasks
#
# Single entry point used by tasks.json for Build / Build (Debug) / Upload.
# Adapt freely — this is a template, not a framework.
#
# The underlying toolchain today is `arduino-cli` (for the RP2040/RP2350
# arduino-pico core), but the script itself is the HAL-level interface —
# point tasks.json at this file regardless of what toolchain you later
# swap in.
#
# Usage:
#   ./scripts/build.sh <build|debug|upload>
#
# Reads from .vscode/settings.json. Keys under `arduino.*` match the
# convention used by the VS Code Arduino extension, so the same
# settings.json stays compatible with that extension if you also use it:
#   - arduino.cliPath               (optional override for the arduino-cli binary)
#   - arduino.fqbn                  (required)
#   - arduino.uploadPort            (required for `upload`)
#   - arduino.sketchbookPath        (optional; used to find libraries/)
#   - arduino.verbose               (optional; `true` → pass -v to the tool)
#   - build.usbManufacturer         (optional; passed as build.usb_manufacturer)
#   - build.usbProduct              (optional; passed as build.usb_product)
#   - build.werror                  (optional; `true` → add -Werror)
#
# On `upload`, this script also starts the persistent serial monitor in the
# background if one is not already running, so log output is visible
# immediately after the device re-enumerates. Adjust/remove to taste.
# =============================================================================
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
SETTINGS_FILE="$PROJECT_DIR/.vscode/settings.json"
BUILD_DIR="$PROJECT_DIR/.build"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC} $*"; }
ok()    { echo -e "${GREEN}[OK]${NC} $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $*"; }
err()   { echo -e "${RED}[ERROR]${NC} $*"; }

usage() {
    err "Usage: $0 <build|debug|upload>"
    exit 2
}

[[ $# -ge 1 ]] || usage
MODE="$1"
case "$MODE" in
    build|debug|upload) ;;
    *) usage ;;
esac

# -----------------------------------------------------------------------------
# Helpers
# -----------------------------------------------------------------------------
read_json_setting() {
    local key="$1" default="${2:-}"
    [[ -f "$SETTINGS_FILE" ]] || { printf '%s\n' "$default"; return 0; }
    python3 - "$SETTINGS_FILE" "$key" "$default" <<'PYEOF' 2>/dev/null
import json, sys
file_path, key, default = sys.argv[1:4]
try:
    with open(file_path) as handle:
        data = json.load(handle)
except Exception:
    print(default)
    raise SystemExit
value = data.get(key, default)
if value is None:
    value = default
print(value)
PYEOF
}

truthy() {
    case "${1:-}" in
        1|true|TRUE|True|yes|YES|Yes|on|ON|On) return 0 ;;
        *) return 1 ;;
    esac
}

find_arduino_cli() {
    local cli_path
    cli_path=$(read_json_setting "arduino.cliPath" "")
    if [[ -n "$cli_path" ]] && command -v "$cli_path" >/dev/null 2>&1; then
        command -v "$cli_path"; return 0
    fi
    if command -v arduino-cli >/dev/null 2>&1; then
        command -v arduino-cli; return 0
    fi
    if [[ -x "$HOME/.local/bin/arduino-cli" ]]; then
        printf '%s\n' "$HOME/.local/bin/arduino-cli"; return 0
    fi
    return 1
}

resolve_libraries_dir() {
    local sketchbook="$1" candidate
    if [[ -n "$sketchbook" && -d "$sketchbook/libraries" ]]; then
        printf '%s\n' "$sketchbook/libraries"; return 0
    fi
    for candidate in "$HOME/libraries" "$HOME/Arduino/libraries"; do
        [[ -d "$candidate" ]] && { printf '%s\n' "$candidate"; return 0; }
    done
    return 1
}

find_sketch() {
    find "$PROJECT_DIR" -maxdepth 2 -name '*.ino' -not -path "*/.build/*" | head -1
}

start_persistent_monitor() {
    local monitor="$PROJECT_DIR/scripts/serial-persistent.py"
    [[ -f "$monitor" ]] || return 0

    # Keep exactly one monitor instance per project to avoid mixed serial output.
    if pgrep -f -- "$PROJECT_DIR/scripts/serial-persistent.py" >/dev/null 2>&1 \
        || pgrep -f -- "$PROJECT_DIR/scripts/serial-monitor.py" >/dev/null 2>&1 \
        || pgrep -f -- "serial-persistent.py .*--project-dir $PROJECT_DIR" >/dev/null 2>&1; then
        return 0
    fi

    nohup python3 "$monitor" --project-dir "$PROJECT_DIR" -m pico \
        >/tmp/persistent-monitor.log 2>&1 &
}

# -----------------------------------------------------------------------------
# Resolve configuration
# -----------------------------------------------------------------------------
CLI="$(find_arduino_cli || true)"
[[ -n "$CLI" ]] || { err "arduino-cli not found (install it or set arduino.cliPath)"; exit 1; }

FQBN="$(read_json_setting "arduino.fqbn" "")"
[[ -n "$FQBN" ]] || { err "arduino.fqbn not set in .vscode/settings.json"; exit 1; }

SKETCH="$(find_sketch || true)"
[[ -n "$SKETCH" ]] || { err "no .ino sketch found under $PROJECT_DIR"; exit 1; }
SKETCH_DIR="$(dirname "$SKETCH")"

SKETCHBOOK="$(read_json_setting "arduino.sketchbookPath" "")"
LIBRARIES_DIR="$(resolve_libraries_dir "$SKETCHBOOK" || true)"
VERBOSE="$(read_json_setting "arduino.verbose" "false")"
USB_MANUFACTURER="$(read_json_setting "build.usbManufacturer" "")"
USB_PRODUCT="$(read_json_setting "build.usbProduct" "")"
WERROR="$(read_json_setting "build.werror" "false")"
PORT=""

if [[ "$MODE" == "upload" ]]; then
    PORT="$(read_json_setting "arduino.uploadPort" "")"
    [[ -n "$PORT" ]] || { err "arduino.uploadPort not set in .vscode/settings.json"; exit 1; }
    start_persistent_monitor
fi

# -----------------------------------------------------------------------------
# Assemble arduino-cli command
# -----------------------------------------------------------------------------
EXTRA_FLAGS="-I '$SKETCH_DIR'"
truthy "$WERROR" && EXTRA_FLAGS="$EXTRA_FLAGS -Werror"

mkdir -p "$BUILD_DIR"
CMD=("$CLI" compile --fqbn "$FQBN" --build-path "$BUILD_DIR")

[[ -n "$LIBRARIES_DIR" ]] && CMD+=(--libraries "$LIBRARIES_DIR")

CMD+=(
    --build-property "compiler.cpp.extra_flags=$EXTRA_FLAGS"
    --build-property "compiler.c.extra_flags=$EXTRA_FLAGS"
)

if [[ -n "$USB_MANUFACTURER" ]]; then
    CMD+=(--build-property "build.usb_manufacturer=\"$USB_MANUFACTURER\"")
fi
if [[ -n "$USB_PRODUCT" ]]; then
    CMD+=(--build-property "build.usb_product=\"$USB_PRODUCT\"")
fi

case "$MODE" in
    build)  ;;
    debug)  CMD+=(--optimize-for-debug) ;;
    upload) CMD+=(--upload --port "$PORT") ;;
esac

truthy "$VERBOSE" && [[ "$MODE" != "debug" ]] && CMD+=(-v)

CMD+=("$SKETCH_DIR")

info "Compiling..."
info "  FQBN:   $FQBN"
info "  Sketch: $SKETCH"
[[ -n "$LIBRARIES_DIR" ]] && info "  Libs:   $LIBRARIES_DIR"
[[ -n "$USB_PRODUCT" ]]  && info "  USB:    $USB_MANUFACTURER / $USB_PRODUCT"
[[ "$MODE" == "upload" && -n "$PORT" ]] && info "  Port:   $PORT"

"${CMD[@]}"

case "$MODE" in
    upload) ok "Upload finished" ;;
    *)      ok "Build finished" ;;
esac
