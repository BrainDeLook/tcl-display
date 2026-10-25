"""Constants for TCL Display."""

from homeassistant.const import Platform

DOMAIN = "tcl_display"
PLATFORMS = [Platform.MEDIA_PLAYER, Platform.SWITCH, Platform.BINARY_SENSOR]
TV_ENTITY = "tv_entity"
VOLUME_ENTITY = "volume_entity"
AUTO_SYNC = "auto_sync"
SOURCES = (
    ("hdmi1", "HDMI 1 Homatics"),
    ("hdmi2", "HDMI 2"),
    ("native", "Google TV"),
    ("hdmi3", "HDMI 3 PC"),
)
PC_SOURCE = "hdmi3"
OFF_STATES = {"off", "standby"}
UNKNOWN_STATES = {"unknown", "unavailable"}


def settings(entry):
    """Return configuration with user options applied."""
    return {**entry.data, **entry.options}
