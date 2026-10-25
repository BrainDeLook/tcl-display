"""Regression coverage for the actual TV dump shapes, with sanitized fixtures."""

import pytest

from .modules import load

parser = load("parser")
BASE = "TvInputHardwareManager Info:\n"


@pytest.mark.parametrize("port", [1, 2, 3])
def test_hardware_port_wins_over_stale_intent(port):
    dump = (
        BASE
        + "intent=content://android.media.tv/passthrough/HW17\n"
        + f"Connection{{ hdmi_port={port}, cable_connection_status=1}}, mInfo: TvInputInfo{{id=HW15}}\n"
    )
    assert parser.detect_source(dump) == f"hdmi{port}"


def test_cec_input_id_does_not_hide_physical_port():
    dump = "sessionStateMap:\n inputId: service/HDMI100004\n" + BASE
    dump += "15: Connection{ hdmi_port=1, mInfo: TvInputInfo{id=HW15}, mCallingUid: 1000 }\n"
    dump += "17: Connection{ hdmi_port=3, mInfo: null, mCallingUid: null }\n"
    assert parser.detect_source(dump) == "hdmi1"


@pytest.mark.parametrize(
    "activity", ["com.gathin.tvhome/.MainActivity", "com.google.android.youtube.tv/.MainActivity"]
)
def test_native_launcher_or_application(activity):
    dump = BASE + "mainSessionToken: null\n hdmi_port=1, mInfo: null\n"
    dump += f"mResumedActivity: ActivityRecord{{ u0 {activity} }}\n"
    dump += "stopped intent=passthrough/HW17\n"
    assert parser.detect_source(dump) == "native"


@pytest.mark.parametrize(
    "dump",
    [
        "",
        "null",
        "adb command failed",
        BASE,
        BASE + "mainSessionToken: null\nmResumedActivity: u0 com.tcl.tv/com.tcl.player.TVActivity",
        BASE + "hdmi_port=1, mInfo: TvInputInfo{x}\nhdmi_port=3, mInfo: TvInputInfo{y}",
        BASE + "hdmi_port=4, mInfo: TvInputInfo{x}",
    ],
)
def test_unknown_or_ambiguous_is_never_guessed(dump):
    assert parser.detect_source(dump) is None


@pytest.mark.parametrize(
    ("state", "source", "expected"),
    [
        ("idle", "hdmi3", True),
        ("playing", "native", False),
        ("idle", "hdmi1", False),
        ("off", None, False),
        ("standby", "hdmi3", False),
        ("unavailable", "hdmi1", None),
        ("unknown", "hdmi3", None),
        ("idle", None, None),
    ],
)
def test_only_confirmed_states_can_command_windows(state, source, expected):
    assert parser.desired_display(state, source) is expected
