"""Real loopback HTTP checks; never change a Windows display."""

from contextlib import asynccontextmanager

import aiohttp
import pytest
from aiohttp import web

from .modules import load

api = load("api")
KEY = "a" * 64


@asynccontextmanager
async def server(handler):
    app = web.Application()
    app.router.add_route("*", "/{path:.*}", handler)
    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, "127.0.0.1", 0)
    await site.start()
    port = site._server.sockets[0].getsockname()[1]
    try:
        async with aiohttp.ClientSession() as session:
            yield api.AgentClient(session, "127.0.0.1", port, KEY)
    finally:
        await runner.cleanup()


def test_connection_string():
    connection = api.Connection.parse(f"tcl-display://192.168.1.10:8766?key={KEY}")
    assert connection.data() == {"host": "192.168.1.10", "port": 8766, "key": KEY}


@pytest.mark.parametrize(
    "value",
    [
        "",
        "http://localhost?key=" + KEY,
        "tcl-display://localhost:0?key=" + KEY,
        "tcl-display://localhost:65536?key=" + KEY,
        "tcl-display://localhost?key=bad",
        "tcl-display://user@localhost?key=" + KEY,
        "tcl-display://localhost/extra?key=" + KEY,
    ],
)
def test_bad_connection_string(value):
    with pytest.raises(ValueError, match="invalid_connection_code"):
        api.Connection.parse(value)


async def test_auth_and_explicit_on_off():
    calls = []

    async def handler(request):
        calls.append((request.method, request.path, request.headers.get("X-TCL-Key")))
        return web.json_response({"ok": True, "enabled": request.path == "/on", "available": True})

    async with server(handler) as client:
        assert (await client.status())["enabled"] is False
        assert (await client.set_enabled(True))["enabled"] is True
        assert (await client.set_enabled(False))["enabled"] is False
    assert calls == [("GET", "/status", KEY), ("POST", "/on", KEY), ("POST", "/off", KEY)]


async def test_invalid_key_is_distinguished():
    async def handler(request):
        return web.Response(status=401)

    async with server(handler) as client:
        with pytest.raises(api.InvalidAuth):
            await client.status()


@pytest.mark.parametrize(
    "payload",
    [
        {"ok": True, "dry_run": True, "enabled": False, "available": True},
        {"ok": True},
        {"ok": False, "enabled": False, "available": True},
        {"ok": True, "enabled": "false", "available": True},
        [],
    ],
)
async def test_agent_response_must_confirm_real_boolean_state(payload):
    async def handler(request):
        return web.json_response(payload)

    async with server(handler) as client:
        with pytest.raises(api.AgentError):
            await client.status()


async def test_display_change_must_be_confirmed():
    async def handler(request):
        return web.json_response({"ok": True, "enabled": False, "available": True})

    async with server(handler) as client:
        with pytest.raises(api.AgentError, match="change_not_confirmed"):
            await client.set_enabled(True)


async def test_redirect_is_not_followed_with_key():
    calls = []

    async def handler(request):
        calls.append(request.path)
        return web.Response(status=302, headers={"Location": "/leak"})

    async with server(handler) as client:
        with pytest.raises(api.AgentError):
            await client.status()
    assert calls == ["/status"]


async def test_chunked_response_is_read_completely():
    async def handler(request):
        response = web.StreamResponse(headers={"Content-Type": "application/json"})
        await response.prepare(request)
        await response.write(b'{"ok":true,')
        await response.write(b'"enabled":false,"available":true}')
        await response.write_eof()
        return response

    async with server(handler) as client:
        assert (await client.status())["enabled"] is False
