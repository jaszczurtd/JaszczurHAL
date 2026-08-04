"""Shared types and validation for OTA firewall platform backends."""

from __future__ import annotations

from dataclasses import dataclass
import ipaddress


RFC1918_NETWORKS = (
    ipaddress.IPv4Network("10.0.0.0/8"),
    ipaddress.IPv4Network("172.16.0.0/12"),
    ipaddress.IPv4Network("192.168.0.0/16"),
)


class SetupError(RuntimeError):
    """Raised when the firewall cannot be configured safely."""


@dataclass(frozen=True)
class NetworkScope:
    interface: str
    network: ipaddress.IPv4Network


def is_rfc1918(network: ipaddress.IPv4Network) -> bool:
    return any(network.subnet_of(private) for private in RFC1918_NETWORKS)
