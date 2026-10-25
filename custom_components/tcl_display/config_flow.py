"""Pair a Windows agent through the Home Assistant UI."""

import probatio as vol
from homeassistant import config_entries
from homeassistant.helpers import selector
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api import AgentClient, AgentError, Connection, InvalidAuth
from .const import DOMAIN


class TclDisplayConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Pair the Windows app in one step."""

    VERSION = 2

    async def async_step_user(self, user_input=None):
        errors = {}
        if user_input is not None:
            try:
                connection = Connection.parse(user_input["connection"])
                client = AgentClient(async_get_clientsession(self.hass), **connection.data())
                await client.status()
            except ValueError:
                errors["base"] = "invalid_connection_code"
            except InvalidAuth:
                errors["base"] = "invalid_auth"
            except AgentError:
                errors["base"] = "cannot_connect"
            else:
                unique_id = f"{connection.host.lower()}:{connection.port}"
                if self.source in {"reconfigure", "reauth"}:
                    entry = (
                        self._get_reauth_entry() if self.source == "reauth" else self._get_reconfigure_entry()
                    )
                    # Host/port is the connection identity; reconfiguration can move it.
                    if any(
                        other.entry_id != entry.entry_id
                        and other.data.get("host") == connection.host
                        and other.data.get("port") == connection.port
                        for other in self._async_current_entries()
                    ):
                        return self.async_abort(reason="already_configured")
                    self.hass.config_entries.async_update_entry(entry, unique_id=unique_id)
                    return self.async_update_reload_and_abort(entry, data_updates=connection.data())
                await self.async_set_unique_id(unique_id)
                self._abort_if_unique_id_configured()
                return self.async_create_entry(
                    title=user_input.get("name", "Windows display"), data=connection.data()
                )
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required("connection"): selector.TextSelector(
                        selector.TextSelectorConfig(type=selector.TextSelectorType.PASSWORD)
                    ),
                    vol.Optional("name", default="Windows display"): str,
                }
            ),
            errors=errors,
        )

    async def async_step_reconfigure(self, user_input=None):
        return await self.async_step_user(user_input)

    async def async_step_reauth(self, entry_data):
        return await self.async_step_user()
