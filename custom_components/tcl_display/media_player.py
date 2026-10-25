"""Virtual TV with real source feedback and existing script controls."""

from homeassistant.components.media_player import (
    MediaPlayerDeviceClass,
    MediaPlayerEntity,
    MediaPlayerEntityFeature,
    MediaPlayerState,
)
from homeassistant.exceptions import HomeAssistantError

from .const import OFF_STATES, SOURCES, TV_ENTITY, UNKNOWN_STATES, VOLUME_ENTITY
from .entity import DisplayEntity


async def async_setup_entry(hass, entry, async_add_entities):
    async_add_entities([TclMediaPlayer(entry.runtime_data)])


class TclMediaPlayer(DisplayEntity, MediaPlayerEntity):
    _attr_name = None
    _attr_device_class = MediaPlayerDeviceClass.TV

    def __init__(self, coordinator):
        super().__init__(coordinator, "tv")
        self._attr_supported_features = (
            MediaPlayerEntityFeature.TURN_ON
            | MediaPlayerEntityFeature.TURN_OFF
            | MediaPlayerEntityFeature.SELECT_SOURCE
        )
        if coordinator.config.get(VOLUME_ENTITY):
            self._attr_supported_features |= (
                MediaPlayerEntityFeature.VOLUME_SET | MediaPlayerEntityFeature.VOLUME_STEP
            )

    @property
    def available(self):
        return self.coordinator.last_update_success and self.coordinator.data.tv_state not in UNKNOWN_STATES

    @property
    def state(self):
        state = self.coordinator.data.tv_state
        if state in OFF_STATES:
            return MediaPlayerState.OFF
        if state in {"playing", "paused", "idle"}:
            return MediaPlayerState(state)
        return MediaPlayerState.ON

    @property
    def source_list(self):
        config = self.coordinator.config
        return [config.get(f"{key}_name", name) for key, name in SOURCES if config.get(f"{key}_script")]

    @property
    def source(self):
        for key, name in SOURCES:
            if key == self.coordinator.data.source:
                return self.coordinator.config.get(f"{key}_name", name)
        return None

    def _volume(self):
        entity = self.coordinator.config.get(VOLUME_ENTITY)
        state = self.hass.states.get(entity) if entity else None
        if state is None or state.state in UNKNOWN_STATES:
            return None
        try:
            low = float(state.attributes["min"])
            high = float(state.attributes["max"])
            if high <= low:
                return None
            return state, low, high
        except KeyError, ValueError, TypeError:
            return None

    @property
    def volume_level(self):
        if info := self._volume():
            state, low, high = info
            try:
                return min(1, max(0, (float(state.state) - low) / (high - low)))
            except ValueError:
                pass
        return None

    async def _tv_action(self, action):
        await self.hass.services.async_call(
            "media_player",
            action,
            {"entity_id": self.coordinator.config[TV_ENTITY]},
            blocking=True,
        )
        await self.coordinator.async_request_refresh()

    async def async_turn_on(self):
        await self._tv_action("turn_on")

    async def async_turn_off(self):
        await self._tv_action("turn_off")

    async def async_select_source(self, source):
        for key, default in SOURCES:
            if source == self.coordinator.config.get(f"{key}_name", default):
                await self.coordinator.async_select_source(key)
                return
        raise HomeAssistantError("Unknown TV source")

    async def async_set_volume_level(self, volume):
        if not (info := self._volume()):
            raise HomeAssistantError("Volume entity is unavailable")
        state, low, high = info
        value = low + min(1, max(0, volume)) * (high - low)
        step = float(state.attributes.get("step", 1))
        if step > 0:
            value = low + round((value - low) / step) * step
        await self.hass.services.async_call(
            "number",
            "set_value",
            {"entity_id": state.entity_id, "value": min(high, max(low, value))},
            blocking=True,
        )

    async def _volume_step(self, action):
        await self.hass.services.async_call(
            "number",
            action,
            {"entity_id": self.coordinator.config[VOLUME_ENTITY]},
            blocking=True,
        )

    async def async_volume_up(self):
        await self._volume_step("increment")

    async def async_volume_down(self):
        await self._volume_step("decrement")
