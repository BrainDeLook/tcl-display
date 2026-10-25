"""Integration-level tests against actual Home Assistant APIs (Linux CI)."""

from unittest.mock import AsyncMock, patch

import pytest

pytest.importorskip("homeassistant")

from homeassistant import data_entry_flow
from homeassistant.core import SupportsResponse
from homeassistant.helpers import entity_registry as er
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.tcl_display.api import AgentError
from custom_components.tcl_display.const import DOMAIN

DATA = {
    "host": "127.0.0.1",
    "port": 8766,
    "key": "a" * 64,
    "tv_entity": "media_player.tcl",
    "volume_entity": "number.jbl_volume",
    "auto_sync": True,
    "hdmi1_name": "HDMI 1 Homatics",
    "hdmi1_script": "script.hdmi1",
    "hdmi2_name": "HDMI 2",
    "hdmi2_script": "script.hdmi2",
    "native_name": "Google TV",
    "native_script": "script.native",
    "hdmi3_name": "HDMI 3 PC",
    "hdmi3_script": "script.hdmi3",
}
NATIVE = "TvInputHardwareManager Info:\nmainSessionToken: null\nmResumedActivity: u0 com.gathin.tvhome/.MainActivity\n"


@pytest.fixture(autouse=True)
def enable_component(enable_custom_integrations):
    yield


@pytest.fixture
async def prepared(hass):
    hass.config.components.add("androidtv")
    hass.states.async_set("media_player.tcl", "idle")
    hass.states.async_set("number.jbl_volume", "30", {"min": 10, "max": 50, "step": 2})
    for script in ("hdmi1", "hdmi2", "native", "hdmi3"):
        hass.states.async_set(f"script.{script}", "off")

        async def run_script(call):
            pass

        hass.services.async_register("script", script, run_script)

    async def adb(call):
        return {"media_player.tcl": {"adb_response": NATIVE}}

    hass.services.async_register("androidtv", "adb_command", adb, supports_response=SupportsResponse.ONLY)
    with (
        patch("custom_components.tcl_display.api.AgentClient.status", new_callable=AsyncMock) as status,
        patch(
            "custom_components.tcl_display.api.AgentClient.set_enabled", new_callable=AsyncMock
        ) as set_enabled,
    ):
        status.return_value = {"ok": True, "enabled": False, "available": True}

        async def set_result(enabled):
            return {"ok": True, "enabled": enabled, "available": True}

        set_enabled.side_effect = set_result
        yield status, set_enabled


async def setup_entry(hass):
    entry = MockConfigEntry(domain=DOMAIN, title="TCL TV", unique_id="127.0.0.1:8766", data=DATA)
    entry.add_to_hass(hass)
    assert await hass.config_entries.async_setup(entry.entry_id)
    await hass.async_block_till_done()
    entities = {
        e.unique_id.rsplit("_", 1)[-1]: e.entity_id
        for e in er.async_entries_for_config_entry(er.async_get(hass), entry.entry_id)
    }
    player = next(
        e.entity_id
        for e in er.async_entries_for_config_entry(er.async_get(hass), entry.entry_id)
        if e.domain == "media_player"
    )
    return entry, player, entities


async def test_ui_flow_and_entities(hass, prepared):
    result = await hass.config_entries.flow.async_init(DOMAIN, context={"source": "user"})
    assert result["type"] is data_entry_flow.FlowResultType.FORM
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {"connection": "tcl-display://127.0.0.1:8766?key=" + "a" * 64}
    )
    assert result["step_id"] == "television"
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"],
        {"tv_entity": DATA["tv_entity"], "volume_entity": DATA["volume_entity"], "auto_sync": True},
    )
    assert result["step_id"] == "sources"
    sources = {key: value for key, value in DATA.items() if key.endswith(("_script", "_name"))}
    result = await hass.config_entries.flow.async_configure(result["flow_id"], sources)
    assert result["type"] is data_entry_flow.FlowResultType.CREATE_ENTRY
    await hass.async_block_till_done()
    assert result["result"].runtime_data.data.source == "native"


async def test_volume_uses_actual_number_range(hass, prepared):
    entry, player, _ = await setup_entry(hass)
    assert hass.states.get(player).attributes["volume_level"] == 0.5
    assert hass.states.get(player).attributes["source"] == "Google TV"
    calls = []

    async def number_set(call):
        calls.append(call.data)

    hass.services.async_register("number", "set_value", number_set)
    await hass.services.async_call(
        "media_player", "volume_set", {"entity_id": player, "volume_level": 0.75}, blocking=True
    )
    assert calls[0]["value"] == 40


async def test_pc_offline_does_not_break_tv(hass, prepared):
    status, _ = prepared
    entry, player, _ = await setup_entry(hass)
    status.side_effect = AgentError("offline")
    await entry.runtime_data.async_refresh()
    await hass.async_block_till_done()
    assert hass.states.get(player).state == "idle"
    assert entry.runtime_data.data.pc_online is False


async def test_unknown_source_never_disconnects_windows(hass, prepared):
    _, enabled = prepared
    entry, _, _ = await setup_entry(hass)

    async def adb_unknown(call):
        return {"media_player.tcl": {"adb_response": "unrecognized"}}

    hass.services.async_register(
        "androidtv", "adb_command", adb_unknown, supports_response=SupportsResponse.ONLY
    )
    enabled.reset_mock()
    await entry.runtime_data.async_refresh()
    enabled.assert_not_awaited()


async def test_source_transition_does_not_undo_pc_activation(hass, prepared):
    _, enabled = prepared
    entry, _, _ = await setup_entry(hass)
    await entry.runtime_data.async_select_source("hdmi3")
    assert enabled.await_count == 1
    enabled.assert_awaited_once_with(True)


async def test_reconfigure_keeps_entry_and_entity_ids(hass, prepared):
    entry, player, _ = await setup_entry(hass)
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": "reconfigure", "entry_id": entry.entry_id}
    )
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"], {"connection": "tcl-display://127.0.0.2:8766?key=" + "b" * 64}
    )
    assert result["type"] is data_entry_flow.FlowResultType.ABORT
    assert entry.data["host"] == "127.0.0.2"
    await hass.async_block_till_done()
    assert len(hass.config_entries.async_entries(DOMAIN)) == 1
    assert er.async_get(hass).async_get(player) is not None


async def test_options_can_remove_optional_volume_and_sources(hass, prepared):
    entry, _, _ = await setup_entry(hass)
    result = await hass.config_entries.options.async_init(entry.entry_id)
    assert result["step_id"] == "init"
    result = await hass.config_entries.options.async_configure(
        result["flow_id"], {"tv_entity": DATA["tv_entity"], "auto_sync": False}
    )
    assert result["step_id"] == "sources"
    values = {key: value for key, value in DATA.items() if key.endswith("_name")}
    values["hdmi3_script"] = DATA["hdmi3_script"]
    result = await hass.config_entries.options.async_configure(result["flow_id"], values)
    assert result["type"] is data_entry_flow.FlowResultType.CREATE_ENTRY
    await hass.async_block_till_done()
    assert entry.options["volume_entity"] == ""
    assert entry.options["hdmi1_script"] == ""
    assert entry.options["native_script"] == ""
    assert entry.runtime_data.config["auto_sync"] is False
