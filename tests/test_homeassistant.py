"""Exercise the switch-only bridge against real Home Assistant APIs in CI."""

from unittest.mock import AsyncMock, patch

import pytest

pytest.importorskip("homeassistant")
from homeassistant import data_entry_flow
from homeassistant.helpers import entity_registry as er
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.tcl_display.api import AgentError, InvalidAuth
from custom_components.tcl_display.const import DOMAIN

DATA = {"host": "127.0.0.1", "port": 8766, "key": "a" * 64}
CODE = "tcl-display://127.0.0.1:8766?key=" + "a" * 64


@pytest.fixture(autouse=True)
def enable_component(enable_custom_integrations):
    yield


@pytest.fixture
async def prepared(hass):
    with (
        patch("custom_components.tcl_display.api.AgentClient.status", new_callable=AsyncMock) as status,
        patch("custom_components.tcl_display.api.AgentClient.set_enabled", new_callable=AsyncMock) as command,
    ):
        status.return_value = {"ok": True, "enabled": False, "available": True, "make_primary": False}

        async def result(enabled):
            return {"ok": True, "enabled": enabled, "available": True, "make_primary": False}

        command.side_effect = result
        yield status, command


async def setup_entry(hass):
    entry = MockConfigEntry(
        domain=DOMAIN, version=2, title="Windows display", unique_id="127.0.0.1:8766", data=DATA
    )
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    entities = er.async_entries_for_config_entry(er.async_get(hass), entry.entry_id)
    assert len(entities) == 2
    return entry, next(e.entity_id for e in entities if e.unique_id.endswith("_pc_display"))


async def test_single_step_pairing(hass, prepared):
    result = await hass.config_entries.flow.async_init(DOMAIN, context={"source": "user"})
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"connection": CODE})
    assert result["type"] is data_entry_flow.FlowResultType.CREATE_ENTRY
    assert result["data"] == DATA
    await hass.async_block_till_done()
    entities = er.async_entries_for_config_entry(er.async_get(hass), result["result"].entry_id)
    assert len(entities) == 2 and all(e.domain == "switch" for e in entities)
    prepared[1].assert_not_awaited()


async def test_commands_and_external_changes(hass, prepared):
    status, command = prepared
    entry, entity = await setup_entry(hass)
    assert hass.states.get(entity).state == "off"
    await hass.services.async_call("switch", "turn_on", {"entity_id": entity}, blocking=True)
    await hass.async_block_till_done()
    assert hass.states.get(entity).state == "on"
    command.assert_awaited_once_with(True)
    # An external Windows change is reflected without undoing it.
    await entry.runtime_data.async_refresh()
    await hass.async_block_till_done()
    assert hass.states.get(entity).state == "off"
    assert command.await_count == 1
    await hass.services.async_call("switch", "turn_off", {"entity_id": entity}, blocking=True)
    command.assert_awaited_with(False)


async def test_offline_and_recovery(hass, prepared):
    status, command = prepared
    entry, entity = await setup_entry(hass)
    status.side_effect = AgentError("offline")
    await entry.runtime_data.async_refresh()
    await hass.async_block_till_done()
    assert hass.states.get(entity).state == "unavailable"
    status.side_effect = None
    await entry.runtime_data.async_refresh()
    await hass.async_block_till_done()
    assert hass.states.get(entity).state == "off"
    command.assert_not_awaited()


async def test_missing_monitor(hass, prepared):
    prepared[0].return_value = {"ok": True, "enabled": False, "available": False}
    _, entity = await setup_entry(hass)
    assert hass.states.get(entity).state == "unavailable"


async def test_failed_command_is_not_optimistic(hass, prepared):
    _, entity = await setup_entry(hass)
    prepared[1].side_effect = AgentError("driver rejected command")
    from homeassistant.exceptions import HomeAssistantError

    with pytest.raises(HomeAssistantError):
        await hass.services.async_call("switch", "turn_on", {"entity_id": entity}, blocking=True)
    await hass.async_block_till_done()
    assert hass.states.get(entity).state == "unavailable"


@pytest.mark.parametrize(
    "error,reason", [(AgentError("offline"), "cannot_connect"), (InvalidAuth("key"), "invalid_auth")]
)
async def test_connection_errors(hass, prepared, error, reason):
    prepared[0].side_effect = error
    result = await hass.config_entries.flow.async_init(DOMAIN, context={"source": "user"})
    result = await hass.config_entries.flow.async_configure(result["flow_id"], {"connection": CODE})
    assert result["errors"] == {"base": reason}


async def test_reconfigure_preserves_switch(hass, prepared):
    entry, entity = await setup_entry(hass)
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": "reconfigure", "entry_id": entry.entry_id}
    )
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {"connection": "tcl-display://127.0.0.2:8766?key=" + "b" * 64}
    )
    assert result["type"] is data_entry_flow.FlowResultType.ABORT
    await hass.async_block_till_done()
    assert entry.data["host"] == "127.0.0.2"
    assert er.async_get(hass).async_get(entity) is not None
    assert len(hass.config_entries.async_entries(DOMAIN)) == 1


async def test_v1_migration_only_removes_own_obsolete_entities(hass, prepared):
    entry = MockConfigEntry(
        domain=DOMAIN,
        version=1,
        title="TCL TV",
        data={
            **DATA,
            "tv_entity": "media_player.tcl",
            "auto_sync": True,
            "hdmi3_script": "script.tcl_hdmi_3",
        },
        options={"auto_sync": True},
    )
    entry.add_to_hass(hass)
    registry = er.async_get(hass)
    for domain, suffix in [
        ("media_player", "tv"),
        ("switch", "auto_sync"),
        ("binary_sensor", "agent_connection"),
        ("switch", "pc_display"),
    ]:
        registry.async_get_or_create(domain, DOMAIN, f"{entry.entry_id}_{suffix}", config_entry=entry)
    unrelated = registry.async_get_or_create("media_player", "universal", "existing_tv")
    switch = registry.async_get_entity_id("switch", DOMAIN, f"{entry.entry_id}_pc_display")
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.version == 2 and dict(entry.data) == DATA and not entry.options
    entities = er.async_entries_for_config_entry(registry, entry.entry_id)
    assert len(entities) == 2 and switch in [e.entity_id for e in entities]
    assert registry.async_get(unrelated.entity_id) is not None
    prepared[1].assert_not_awaited()


async def test_primary_mode_is_independent_and_has_no_display_command(hass, prepared):
    entry, display = await setup_entry(hass)
    registry = er.async_get(hass)
    mode = registry.async_get_entity_id("switch", DOMAIN, f"{entry.entry_id}_make_primary")
    assert hass.states.get(mode).state == "off"
    with patch(
        "custom_components.tcl_display.api.AgentClient.set_primary_mode", new_callable=AsyncMock
    ) as set_mode:
        set_mode.return_value = {"ok": True, "enabled": False, "available": True, "make_primary": True}
        await hass.services.async_call("switch", "turn_on", {"entity_id": mode}, blocking=True)
        await hass.async_block_till_done()
        assert hass.states.get(mode).state == "on"
        assert hass.states.get(display).state == "off"
        set_mode.assert_awaited_once_with(True)
        prepared[1].assert_not_awaited()
        set_mode.return_value["make_primary"] = False
        await hass.services.async_call("switch", "turn_off", {"entity_id": mode}, blocking=True)
        set_mode.assert_awaited_with(False)


async def test_older_agent_only_disables_new_mode_switch(hass, prepared):
    prepared[0].return_value = {"ok": True, "enabled": False, "available": True}
    entry, display = await setup_entry(hass)
    mode = er.async_get(hass).async_get_entity_id("switch", DOMAIN, f"{entry.entry_id}_make_primary")
    assert hass.states.get(display).state == "off"
    assert hass.states.get(mode).state == "unavailable"
