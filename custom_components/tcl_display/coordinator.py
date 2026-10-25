"""Poll Windows without issuing unsolicited display commands."""

import asyncio
import logging
from dataclasses import dataclass
from datetime import timedelta

from homeassistant.exceptions import ConfigEntryAuthFailed, HomeAssistantError
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .api import AgentClient, AgentError, InvalidAuth
from .const import DOMAIN

_LOGGER = logging.getLogger(__name__)


@dataclass(frozen=True)
class DisplayData:
    enabled: bool
    available: bool
    make_primary: bool | None = None


class DisplayCoordinator(DataUpdateCoordinator[DisplayData]):
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
        self.client = AgentClient(
            async_get_clientsession(hass), entry.data["host"], entry.data["port"], entry.data["key"]
        )
        self.command_lock = asyncio.Lock()

    async def _async_update_data(self):
        async with self.command_lock:
            try:
                status = await self.client.status()
            except InvalidAuth as err:
                raise ConfigEntryAuthFailed("Invalid Windows agent key") from err
            except AgentError as err:
                raise UpdateFailed("Windows agent unavailable") from err
            return DisplayData(status["enabled"], status["available"], status.get("make_primary"))

    async def async_set_pc(self, enabled):
        async with self.command_lock:
            try:
                status = await self.client.set_enabled(enabled)
            except InvalidAuth as err:
                raise ConfigEntryAuthFailed("Invalid Windows agent key") from err
            except AgentError as err:
                # A failed command must not leave a previously successful state visible.
                self.async_set_update_error(UpdateFailed("Display change not confirmed"))
                raise HomeAssistantError("Windows did not confirm the display change") from err
            self.async_set_updated_data(
                DisplayData(status["enabled"], status["available"], status.get("make_primary"))
            )

    async def async_set_primary_mode(self, enabled):
        async with self.command_lock:
            try:
                status = await self.client.set_primary_mode(enabled)
            except InvalidAuth as err:
                raise ConfigEntryAuthFailed("Invalid Windows agent key") from err
            except AgentError as err:
                raise HomeAssistantError("Windows did not confirm the primary-display mode") from err
            self.async_set_updated_data(
                DisplayData(status["enabled"], status["available"], status["make_primary"])
            )
