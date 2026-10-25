"""Read actual TV hardware paths, never stale activity intent URIs."""

import re

_CONNECTION = re.compile(r"hdmi_port=(\d+)[^\r\n]*mInfo: TvInputInfo\{")
_RESUMED = re.compile(r"mResumedActivity: ([^\r\n]+)")


def detect_source(dump: str) -> str | None:
    """Return a confirmed input, or None for ambiguous/transitional output."""
    if "TvInputHardwareManager Info:" not in dump:
        return None
    ports = set(_CONNECTION.findall(dump))
    if len(ports) == 1:
        port = next(iter(ports))
        return f"hdmi{port}" if port in {"1", "2", "3"} else None
    if ports:
        return None
    resumed = _RESUMED.findall(dump)
    if (
        "mainSessionToken: null" in dump
        and resumed
        and all("com.tcl.tv/" not in activity for activity in resumed)
    ):
        return "native"
    return None


def desired_display(tv_state: str, source: str | None) -> bool | None:
    """Unknown TV/input states must never disconnect the PC screen."""
    if tv_state in {"off", "standby"}:
        return False
    if tv_state in {"unknown", "unavailable"}:
        return None
    if source in {"hdmi1", "hdmi2", "hdmi3", "native"}:
        return source == "hdmi3"
    return None
