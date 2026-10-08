"""Isolated scanner installations for the Linux tool and scan tests."""

from __future__ import annotations

import json
from pathlib import Path
import stat
import sys
import venv


def install_cve(path: Path, script: str, version: str,
                source_hash: str | None) -> Path:
    """Create a pipx-like entry point and real Python distribution metadata."""
    environment = path.parent.parent / "share" / "pipx" / "venvs" / "cve-bin-tool"
    venv.EnvBuilder(with_pip=False, symlinks=True).create(environment)
    packages = (environment / "lib" /
                f"python{sys.version_info.major}.{sys.version_info.minor}" /
                "site-packages")
    metadata = packages / "cve_bin_tool.dist-info"
    metadata.mkdir(parents=True, exist_ok=True)
    (metadata / "METADATA").write_text(
        f"Metadata-Version: 2.1\nName: cve-bin-tool\nVersion: {version}\n",
        encoding="utf-8")
    record = metadata / "direct_url.json"
    if source_hash is None:
        record.unlink(missing_ok=True)
    else:
        record.write_text(json.dumps({
            "url": "file:///removed-download/source.tar.gz",
            "archive_info": {"hashes": {"sha256": source_hash}},
        }), encoding="utf-8")
    entry = environment / "bin" / "cve-bin-tool"
    entry.write_text(script, encoding="utf-8")
    entry.chmod(entry.stat().st_mode | stat.S_IXUSR)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.unlink(missing_ok=True)
    path.symlink_to(entry)
    return metadata
