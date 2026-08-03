"""Shared serial identity matching for Linux and Windows hosts."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Iterable

from vscode.runtime.platform_api import SerialPortRecord


IDENTITY_VERIFIED = "verified"
IDENTITY_MISMATCH = "mismatch"
IDENTITY_MISSING_METADATA = "missing-metadata"
IDENTITY_NOT_CONFIGURED = "not-configured"


def normalize_identity_text(value: str) -> str:
    """Normalize descriptor text without depending on host separators."""

    return "".join(character.lower() for character in value if character.isalnum())


def _optional_text(value: Any) -> str:
    return str(value).strip() if value is not None else ""


def _optional_usb_id(value: Any, field_name: str) -> int | None:
    if isinstance(value, int):
        if 0 <= value <= 0xFFFF:
            return value
        raise ValueError(f"{field_name} must be in range 0..65535")
    if not isinstance(value, str) or not value.strip():
        return None
    text = value.strip().lower()
    try:
        base = 16 if (
            text.startswith("0x")
            or len(text) == 4
            or any(ch in "abcdef" for ch in text)
        ) else 10
        parsed = int(text, base)
    except ValueError as exc:
        raise ValueError(
            f"{field_name} must be an integer or a USB hexadecimal identifier"
        ) from exc
    if not 0 <= parsed <= 0xFFFF:
        raise ValueError(f"{field_name} must be in range 0..65535")
    return parsed


@dataclass(frozen=True)
class SerialIdentityExpectation:
    manufacturer: str = ""
    product: str = ""
    serial_number: str = ""
    interface: str = ""
    location: str = ""
    by_id_hint: str = ""
    vid: int | None = None
    pid: int | None = None

    @classmethod
    def from_config(cls, identity: dict[str, Any] | None) -> "SerialIdentityExpectation":
        value = identity if isinstance(identity, dict) else {}
        return cls(
            manufacturer=_optional_text(value.get("usbManufacturer")),
            product=_optional_text(value.get("usbProduct")),
            serial_number=_optional_text(value.get("usbSerialNumber")),
            interface=_optional_text(value.get("usbInterface")),
            location=_optional_text(value.get("usbLocation")),
            by_id_hint=_optional_text(value.get("byIdHint")),
            vid=_optional_usb_id(value.get("usbVid"), "usbVid"),
            pid=_optional_usb_id(value.get("usbPid"), "usbPid"),
        )

    def configured(self) -> bool:
        return any(
            (
                self.manufacturer,
                self.product,
                self.serial_number,
                self.interface,
                self.location,
                self.by_id_hint,
                self.vid is not None,
                self.pid is not None,
            )
        )

    def as_dict(self) -> dict[str, Any]:
        return {
            "usbManufacturer": self.manufacturer,
            "usbProduct": self.product,
            "usbSerialNumber": self.serial_number,
            "usbInterface": self.interface,
            "usbLocation": self.location,
            "byIdHint": self.by_id_hint,
            "usbVid": self.vid,
            "usbPid": self.pid,
        }


@dataclass(frozen=True)
class SerialIdentityMatch:
    status: str
    score: int
    matched_fields: tuple[str, ...] = ()
    missing_fields: tuple[str, ...] = ()
    mismatched_fields: tuple[str, ...] = ()

    @property
    def verified(self) -> bool:
        return self.status == IDENTITY_VERIFIED

    def reason(self) -> str:
        if self.status == IDENTITY_VERIFIED:
            return "matched " + ", ".join(self.matched_fields)
        if self.status == IDENTITY_MISSING_METADATA:
            return "missing " + ", ".join(self.missing_fields)
        if self.status == IDENTITY_MISMATCH:
            return "mismatched " + ", ".join(self.mismatched_fields)
        return "no USB identity configured"


def _text_matches(expected: str, actual: str) -> bool:
    expected_normalized = normalize_identity_text(expected)
    actual_normalized = normalize_identity_text(actual)
    return bool(
        expected_normalized
        and actual_normalized
        and (
            expected_normalized == actual_normalized
            or expected_normalized in actual_normalized
        )
    )


def _identity_haystacks(record: SerialPortRecord) -> tuple[str, ...]:
    values = [
        record.manufacturer,
        record.product,
        record.interface,
        record.serial_number,
        record.location,
        record.hwid,
        record.description,
        record.platform_identity,
        f"{record.manufacturer} {record.product}",
        *record.aliases,
    ]
    return tuple(
        normalized
        for normalized in (normalize_identity_text(value) for value in values)
        if normalized
    )


def match_serial_identity(
    record: SerialPortRecord,
    expected: SerialIdentityExpectation,
) -> SerialIdentityMatch:
    """Match every configured stable field and score diagnostic evidence."""

    if not expected.configured():
        return SerialIdentityMatch(IDENTITY_NOT_CONFIGURED, 0)

    matched: list[str] = []
    missing: list[str] = []
    mismatched: list[str] = []
    score = 0

    text_fields = (
        ("manufacturer", expected.manufacturer, record.manufacturer, 25),
        ("product", expected.product, record.product, 40),
        ("serialNumber", expected.serial_number, record.serial_number, 100),
        ("interface", expected.interface, record.interface, 15),
        ("location", expected.location, record.location, 10),
    )
    for name, wanted, actual, weight in text_fields:
        if not wanted:
            continue
        if not actual:
            missing.append(name)
        elif _text_matches(wanted, actual):
            matched.append(name)
            score += weight
        else:
            mismatched.append(name)

    numeric_fields = (
        ("vid", expected.vid, record.vid, 30),
        ("pid", expected.pid, record.pid, 30),
    )
    for name, wanted, actual, weight in numeric_fields:
        if wanted is None:
            continue
        if actual is None:
            missing.append(name)
        elif wanted == actual:
            matched.append(name)
            score += weight
        else:
            mismatched.append(name)

    hint_matched = False
    if expected.by_id_hint:
        hint = normalize_identity_text(expected.by_id_hint)
        hint_matched = bool(hint and any(hint in value for value in _identity_haystacks(record)))
        if hint_matched:
            matched.append("byIdHint")
            score += 20

    required_fields_present = any(
        (
            expected.manufacturer,
            expected.product,
            expected.serial_number,
            expected.interface,
            expected.location,
            expected.vid is not None,
            expected.pid is not None,
        )
    )
    if not required_fields_present and expected.by_id_hint and not hint_matched:
        if _identity_haystacks(record):
            mismatched.append("byIdHint")
        else:
            missing.append("byIdHint")

    if mismatched:
        return SerialIdentityMatch(
            IDENTITY_MISMATCH,
            score,
            tuple(matched),
            tuple(missing),
            tuple(mismatched),
        )
    if (
        record.platform == "windows"
        and missing == ["manufacturer"]
        and {"product", "vid", "pid"}.issubset(matched)
        and normalize_identity_text(expected.product)
        == normalize_identity_text(record.product)
    ):
        return SerialIdentityMatch(
            IDENTITY_VERIFIED,
            score,
            tuple(matched),
            tuple(missing),
        )
    if missing:
        return SerialIdentityMatch(
            IDENTITY_MISSING_METADATA,
            score,
            tuple(matched),
            tuple(missing),
        )
    return SerialIdentityMatch(IDENTITY_VERIFIED, score, tuple(matched))


def verified_serial_records(
    records: Iterable[SerialPortRecord],
    expected: SerialIdentityExpectation,
) -> list[tuple[SerialPortRecord, SerialIdentityMatch]]:
    matches = [
        (record, match)
        for record in records
        if (match := match_serial_identity(record, expected)).verified
    ]
    return sorted(matches, key=lambda item: (-item[1].score, item[0].device.casefold()))
