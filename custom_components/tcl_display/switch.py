"""One switch controlling the selected Windows display."""

from homeassistant.components.switch import SwitchEntity

from .entity import DisplayEntity


async def async_setup_entry(hass, entry, async_add_entities):
    async_add_entities([PcDisplaySwitch(entry.runtime_data)])


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
