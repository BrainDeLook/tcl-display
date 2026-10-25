"""One switch controlling the selected Windows display."""

from homeassistant.components.switch import SwitchEntity
from homeassistant.const import EntityCategory

from .entity import DisplayEntity


async def async_setup_entry(hass, entry, async_add_entities):
    async_add_entities([PcDisplaySwitch(entry.runtime_data), PrimaryModeSwitch(entry.runtime_data)])


class PcDisplaySwitch(DisplayEntity, SwitchEntity):
    _attr_translation_key = "pc_display"
    _attr_icon = "mdi:monitor"

    def __init__(self, coordinator):
        super().__init__(coordinator, "pc_display")

    @property
    def available(self):
        return super().available and self.coordinator.data.available

    @property
    def is_on(self):
        return self.coordinator.data.enabled

    async def async_turn_on(self, **kwargs):
        await self.coordinator.async_set_pc(True)

    async def async_turn_off(self, **kwargs):
        await self.coordinator.async_set_pc(False)


class PrimaryModeSwitch(DisplayEntity, SwitchEntity):
    """Preference only; changing it never changes the desktop immediately."""

    _attr_translation_key = "make_primary"
    _attr_icon = "mdi:monitor-star"
    _attr_entity_category = EntityCategory.CONFIG

    def __init__(self, coordinator):
        super().__init__(coordinator, "make_primary")

    @property
    def available(self):
        return super().available and self.coordinator.data.make_primary is not None

    @property
    def is_on(self):
        return self.coordinator.data.make_primary

    async def async_turn_on(self, **kwargs):
        await self.coordinator.async_set_primary_mode(True)

    async def async_turn_off(self, **kwargs):
        await self.coordinator.async_set_primary_mode(False)
