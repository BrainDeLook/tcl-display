"""TV feedback, PC status, and serialized input changes."""

import asyncio
import logging
from dataclasses import dataclass
from datetime import timedelta

from homeassistant.exceptions import ConfigEntryAuthFailed, HomeAssistantError
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator

from .api import AgentClient, AgentError, InvalidAuth
from .const import AUTO_SYNC, DOMAIN, OFF_STATES, PC_SOURCE, TV_ENTITY, UNKNOWN_STATES, settings
from .parser import desired_display, detect_source

_LOGGER = logging.getLogger(__name__)


@dataclass(frozen=True)
class DisplayData:
    tv_state: str
    source: str | None
    pc_online: bool
    pc_enabled: bool | None
    pc_available: bool
    adb_ok: bool


class DisplayCoordinator(DataUpdateCoordinator[DisplayData]):
    """All automatic commands use actual fresh TV feedback."""

    def __init__(self, hass, entry):
        super().__init__(
            hass,
            _LOGGER,
            name=DOMAIN,
            config_entry=entry,
            update_interval=timedelta(seconds=15),
            always_update=False,
        )
        self.entry = entry
        self.config = settings(entry)
        self.client = AgentClient(
            async_get_clientsession(hass),
            entry.data["host"],
            entry.data["port"],
            entry.data["key"],
        )
        self.command_lock = asyncio.Lock()
        self._pending_source = None
        self._pending_until = 0.0

    async def _async_update_data(self):
        async with self.command_lock:
            return await self._read_and_sync()

    async def _read_and_sync(self):
        tv = self.hass.states.get(self.config[TV_ENTITY])
        tv_state = tv.state if tv else "unavailable"
        source = None
        adb_ok = False
        if tv_state not in OFF_STATES | UNKNOWN_STATES:
            try:
                async with asyncio.timeout(15):
                    result = await self.hass.services.async_call(
                        "androidtv",
                        "adb_command",
                        {
                            "entity_id": self.config[TV_ENTITY],
                            "command": "dumpsys tv_input; dumpsys activity activities",
                        },
                        blocking=True,
                        return_response=True,
                    )
                response = (result or {}).get(self.config[TV_ENTITY]) or {}
                dump = response.get("adb_response", "")
                if isinstance(dump, str):
                    source = detect_source(dump)
                    adb_ok = source is not None
            except HomeAssistantError, TimeoutError:
                _LOGGER.debug("TV ADB query failed; source will remain unknown")
        online, enabled, available = False, None, False
        try:
            status = await self.client.status()
            online, enabled, available = True, status["enabled"], status["available"]
            desired = desired_display(tv_state, source)
            if self._pending_source:
                if source == self._pending_source or self.hass.loop.time() >= self._pending_until:
                    self._pending_source = None
                elif tv_state not in OFF_STATES:
                    desired = None
            if self.config.get(AUTO_SYNC, True) and desired is not None and enabled != desired:
                status = await self.client.set_enabled(desired)
                enabled, available = status["enabled"], status["available"]
        except InvalidAuth as err:
            raise ConfigEntryAuthFailed("Invalid Windows agent key") from err
        except AgentError:
            # TV remains usable when the PC is asleep or unavailable.
            _LOGGER.debug("Windows agent unavailable or display command not confirmed")
        return DisplayData(tv_state, source, online, enabled, available, adb_ok)

    async def async_select_source(self, source_id):
        """Do not let polling turn the screen off during a source change."""
        script = self.config.get(f"{source_id}_script")
        if not script:
            raise HomeAssistantError("No script configured for this source")
        async with self.command_lock:
            await self.hass.services.async_call(
                "script",
                script.removeprefix("script."),
                {},
                blocking=True,
            )
            self._pending_source = source_id
            self._pending_until = self.hass.loop.time() + 30
            if source_id == PC_SOURCE and self.config.get(AUTO_SYNC, True):
                # Switch the TV first so the graphics driver can read its EDID.
                await asyncio.sleep(2)
                try:
                    await self.client.set_enabled(True)
                except AgentError as err:
                    raise HomeAssistantError("Windows did not confirm display activation") from err
        await self.async_request_refresh()

    async def async_set_pc(self, enabled):
        async with self.command_lock:
            try:
                await self.client.set_enabled(enabled)
            except AgentError as err:
                raise HomeAssistantError("Windows did not confirm the display change") from err
        # Read fresh status without immediately undoing a manual command.
        data = self.data
        self.async_set_updated_data(
            DisplayData(
                data.tv_state,
                data.source,
                True,
                enabled,
                data.pc_available,
                data.adb_ok,
            )
        )
