#!/usr/bin/env python3
"""Validate formatting-independent structural source checks."""

from source_assertions import (
    source_fragment_position,
    source_has_fragment,
    source_section,
)


formatted = """
hal_status_t connect(void) {
  return hid_host_connect (
      s_candidate_address,
      HID_PROTOCOL_MODE_REPORT,
      & hid_cid);
}
"""

assert source_has_fragment(
    formatted,
    "hid_host_connect(s_candidate_address, HID_PROTOCOL_MODE_REPORT, &hid_cid)",
)
assert not source_has_fragment(formatted, "HID_PROTOCOL_MODE_BOOT")
assert not source_has_fragment('command("DISC OVER")', 'command("DISCOVER")')
assert source_fragment_position(formatted, "return hid_host_connect(") > 0

body = source_section(
    formatted,
    "hal_status_t connect(void)",
    "}",
)
assert source_has_fragment(body, "return hid_host_connect(")

try:
    source_has_fragment(formatted, "   ")
except ValueError:
    pass
else:
    raise AssertionError("empty source fragment was accepted")

try:
    source_fragment_position(formatted, "missing_call()")
except AssertionError:
    pass
else:
    raise AssertionError("missing source fragment did not fail")
