"""Set up the Windows display switch."""

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers import entity_registry as er

from .const import PLATFORMS
from .coordinator import DisplayCoordinator


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    coordinator = DisplayCoordinator(hass, entry)
    entry.runtime_data = coordinator
    await coordinator.async_config_entry_first_refresh()
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)


async def async_migrate_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Keep the display switch and remove only obsolete bridge entities/settings."""
    if entry.version > 2:
        return False
    if entry.version == 1:
        registry = er.async_get(hass)
        obsolete = {f"{entry.entry_id}_{suffix}" for suffix in ("tv", "auto_sync", "agent_connection")}
        for entity in er.async_entries_for_config_entry(registry, entry.entry_id):
            if entity.platform == "tcl_display" and entity.unique_id in obsolete:
                registry.async_remove(entity.entity_id)
        hass.config_entries.async_update_entry(
            entry,
            data={key: entry.data[key] for key in ("host", "port", "key")},
            options={},
            version=2,
        )
    return True
