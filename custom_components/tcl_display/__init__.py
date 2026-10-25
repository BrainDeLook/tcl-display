"""Set up TCL Display from Home Assistant's user interface."""

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant, callback
from homeassistant.helpers.event import async_track_state_change_event

from .const import PLATFORMS, TV_ENTITY, VOLUME_ENTITY, settings
from .coordinator import DisplayCoordinator


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    coordinator = DisplayCoordinator(hass, entry)
    entry.runtime_data = coordinator
    await coordinator.async_config_entry_first_refresh()
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)

    async def tv_changed(event):
        if event.data.get("old_state") is None or event.data.get("new_state") is None:
            await coordinator.async_request_refresh()
        elif event.data["old_state"].state != event.data["new_state"].state:
            await coordinator.async_request_refresh()

    entry.async_on_unload(
        async_track_state_change_event(
            hass,
            [settings(entry)[TV_ENTITY]],
            tv_changed,
        )
    )
    if volume := settings(entry).get(VOLUME_ENTITY):

        @callback
        def volume_changed(event):
            coordinator.async_update_listeners()

        entry.async_on_unload(async_track_state_change_event(hass, [volume], volume_changed))
    entry.async_on_unload(entry.add_update_listener(async_reload_entry))
    return True


async def async_reload_entry(hass: HomeAssistant, entry: ConfigEntry):
    await hass.config_entries.async_reload(entry.entry_id)


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    return await hass.config_entries.async_unload_platforms(entry, PLATFORMS)
