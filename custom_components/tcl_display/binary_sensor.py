"""Connectivity diagnostics."""

from homeassistant.components.binary_sensor import BinarySensorDeviceClass, BinarySensorEntity
from homeassistant.const import EntityCategory

from .entity import DisplayEntity


async def async_setup_entry(hass, entry, async_add_entities):
    async_add_entities([AgentConnection(entry.runtime_data)])


class AgentConnection(DisplayEntity, BinarySensorEntity):
    _attr_translation_key = "agent_connection"
    _attr_device_class = BinarySensorDeviceClass.CONNECTIVITY
    _attr_entity_category = EntityCategory.DIAGNOSTIC

    def __init__(self, coordinator):
        super().__init__(coordinator, "agent_connection")

    @property
    def is_on(self):
        return self.coordinator.data.pc_online
