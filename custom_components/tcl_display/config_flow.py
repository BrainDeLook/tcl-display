"""Configure the complete TV/PC bridge through the HA UI."""

import probatio as vol
from homeassistant import config_entries
from homeassistant.core import callback
from homeassistant.helpers import entity_registry as er
from homeassistant.helpers import selector
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .api import AgentClient, AgentError, Connection, InvalidAuth
from .const import AUTO_SYNC, DOMAIN, SOURCES, TV_ENTITY, VOLUME_ENTITY, settings


def tv_schema(hass, defaults):
    schema = {
        vol.Required(TV_ENTITY, default=defaults.get(TV_ENTITY, "media_player.tcl")): selector.EntitySelector(
            selector.EntitySelectorConfig(domain="media_player")
        ),
        vol.Optional(
            VOLUME_ENTITY, **({"default": defaults[VOLUME_ENTITY]} if defaults.get(VOLUME_ENTITY) else {})
        ): selector.EntitySelector(selector.EntitySelectorConfig(domain="number")),
        vol.Required(AUTO_SYNC, default=defaults.get(AUTO_SYNC, True)): bool,
    }
    return vol.Schema(schema)


def source_schema(hass, defaults):
    schema = {}
    guesses = {
        "hdmi1": "script.tcl_hdmi_1",
        "hdmi2": "script.tcl_hdmi_2",
        "native": "script.google_tv_domoj",
        "hdmi3": "script.tcl_hdmi_3",
    }
    for key, name in SOURCES:
        schema[vol.Required(f"{key}_name", default=defaults.get(f"{key}_name", name))] = str
        value = defaults.get(f"{key}_script")
        if not value and hass.states.get(guesses[key]):
            value = guesses[key]
        marker = vol.Required if key == "hdmi3" else vol.Optional
        schema[marker(f"{key}_script", **({"default": value} if value else {}))] = selector.EntitySelector(
            selector.EntitySelectorConfig(domain="script")
        )
    return vol.Schema(schema)


def valid_tv(hass, values):
    tv = values.get(TV_ENTITY, "")
    registry_entry = er.async_get(hass).async_get(tv)
    return bool(hass.states.get(tv)) and not (registry_entry and registry_entry.platform == DOMAIN)


def valid_sources(values):
    names = [values[f"{key}_name"].strip() for key, _ in SOURCES]
    return all(names) and len(names) == len(set(names))


class TclDisplayConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Connection -> existing TV/volume -> existing source scripts."""

    VERSION = 1

    def __init__(self):
        self._data = {}

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
                self._data = {**connection.data(), "name": user_input.get("name", "TCL TV")}
                return await self.async_step_television()
        return self.async_show_form(
            step_id="user",
            data_schema=vol.Schema(
                {
                    vol.Required("connection"): selector.TextSelector(
                        selector.TextSelectorConfig(type=selector.TextSelectorType.PASSWORD)
                    ),
                    vol.Optional("name", default="TCL TV"): str,
                }
            ),
            errors=errors,
        )

    async def async_step_television(self, user_input=None):
        errors = {}
        if user_input is not None:
            if valid_tv(self.hass, user_input):
                self._data.update(user_input)
                return await self.async_step_sources()
            errors["base"] = "invalid_tv"
        return self.async_show_form(
            step_id="television", data_schema=tv_schema(self.hass, user_input or self._data), errors=errors
        )

    async def async_step_sources(self, user_input=None):
        errors = {}
        if user_input is not None:
            if valid_sources(user_input):
                self._data.update(user_input)
                return self.async_create_entry(title=self._data.pop("name"), data=self._data)
            errors["base"] = "duplicate_source_names"
        return self.async_show_form(
            step_id="sources", data_schema=source_schema(self.hass, user_input or self._data), errors=errors
        )

    async def async_step_reconfigure(self, user_input=None):
        return await self.async_step_user(user_input)

    async def async_step_reauth(self, entry_data):
        return await self.async_step_user()

    @staticmethod
    @callback
    def async_get_options_flow(config_entry):
        return TclDisplayOptionsFlow()


class TclDisplayOptionsFlow(config_entries.OptionsFlow):
    """Change selected entities and sources without YAML."""

    def __init__(self):
        self._values = {}

    async def async_step_init(self, user_input=None):
        errors = {}
        if user_input is not None:
            if valid_tv(self.hass, user_input):
                self._values = dict(user_input)
                # An omitted optional value must clear a previous volume entity.
                self._values.setdefault(VOLUME_ENTITY, "")
                return await self.async_step_sources()
            errors["base"] = "invalid_tv"
        return self.async_show_form(
            step_id="init",
            data_schema=tv_schema(self.hass, user_input or settings(self.config_entry)),
            errors=errors,
        )

    async def async_step_sources(self, user_input=None):
        errors = {}
        if user_input is not None:
            if valid_sources(user_input):
                self._values.update(user_input)
                for key, _ in SOURCES:
                    self._values.setdefault(f"{key}_script", "")
                return self.async_create_entry(title="", data=self._values)
            errors["base"] = "duplicate_source_names"
        return self.async_show_form(
            step_id="sources",
            data_schema=source_schema(self.hass, user_input or settings(self.config_entry)),
            errors=errors,
        )
