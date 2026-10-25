"""Manual Windows display control; automatic sync is configured in options."""

from homeassistant.components.switch import SwitchEntity
from homeassistant.const import EntityCategory

from .const import AUTO_SYNC
from .entity import DisplayEntity


async def async_setup_entry(hass, entry, async_add_entities):
    async_add_entities([PcDisplaySwitch(entry.runtime_data), AutoSyncSwitch(entry.runtime_data)])


class PcDisplaySwitch(DisplayEntity, SwitchEntity):
    _attr_translation_key = "pc_display"
    _attr_icon = "mdi:monitor"

    def __init__(self, coordinator):
        super().__init__(coordinator, "pc_display")

    @property
    def available(self):
        return super().available and self.coordinator.data.pc_online

    @property
    def is_on(self):
        return self.coordinator.data.pc_enabled

    async def async_turn_on(self, **kwargs):
        await self.coordinator.async_set_pc(True)

    async def async_turn_off(self, **kwargs):
        await self.coordinator.async_set_pc(False)


class AutoSyncSwitch(DisplayEntity, SwitchEntity):
    _attr_translation_key = "auto_sync"
    _attr_icon = "mdi:sync"
    _attr_entity_category = EntityCategory.CONFIG

    def __init__(self, coordinator):
        super().__init__(coordinator, "auto_sync")

    @property
    def available(self):
        return True

    @property
    def is_on(self):
        return self.coordinator.config.get(AUTO_SYNC, True)

    async def _set(self, enabled):
        entry = self.coordinator.entry
        self.hass.config_entries.async_update_entry(entry, options={**entry.options, AUTO_SYNC: enabled})

    async def async_turn_on(self, **kwargs):
        await self._set(True)

    async def async_turn_off(self, **kwargs):
        await self._set(False)
