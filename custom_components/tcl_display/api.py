"""Small authenticated HTTP client for the native Windows agent."""

import asyncio
import json
import re
from dataclasses import dataclass
from urllib.parse import parse_qs, urlsplit

import aiohttp


class AgentError(Exception):
    """Agent could not complete a request."""


class InvalidAuth(AgentError):
    """Wrong agent key."""


@dataclass(frozen=True)
class Connection:
    """Connection information; never include credentials in logs."""

    host: str
    port: int
    key: str

    @classmethod
    def parse(cls, code: str):
        """Parse the one-line code copied from the Windows app."""
        try:
            url = urlsplit(code.strip())
            key = parse_qs(url.query, strict_parsing=True).get("key", [""])[0]
            host = url.hostname
            port = url.port if url.port is not None else 8766
            if (
                url.scheme != "tcl-display"
                or not host
                or url.username is not None
                or url.password is not None
                or url.path not in {"", "/"}
                or url.fragment
                or not re.fullmatch(r"[a-zA-Z0-9.-]+", host)
                or not 1 <= port <= 65535
                or not re.fullmatch(r"[0-9a-fA-F]{64}", key)
            ):
                raise ValueError
            return cls(host, port, key)
        except (ValueError, TypeError) as err:
            raise ValueError("invalid_connection_code") from err

    def data(self):
        """Return serializable config data."""
        return {"host": self.host, "port": self.port, "key": self.key}


class AgentClient:
    """Use HA's shared session; do not create another client or event loop."""

    def __init__(self, session, host: str, port: int, key: str):
        self._session = session
        self._base = f"http://{host}:{port}"
        self._headers = {"X-TCL-Key": key}

    async def request(self, path: str, method="GET") -> dict:
        """Bound request time, response size and redirects."""
        try:
            async with self._session.request(
                method,
                self._base + path,
                headers=self._headers,
                timeout=aiohttp.ClientTimeout(total=10),
                allow_redirects=False,
            ) as response:
                if response.status == 401:
                    raise InvalidAuth("invalid_auth")
                if response.status != 200:
                    raise AgentError("agent_request_failed")
                raw = bytearray()
                async for chunk in response.content.iter_chunked(4096):
                    raw.extend(chunk)
                    if len(raw) > 8192:
                        raise AgentError("invalid_response")
                result = json.loads(raw)
                if (
                    not isinstance(result, dict)
                    or result.get("ok") is not True
                    or result.get("dry_run")
                    or not isinstance(result.get("enabled"), bool)
                    or not isinstance(result.get("available"), bool)
                ):
                    raise AgentError("invalid_response")
                return result
        except (aiohttp.ClientError, asyncio.TimeoutError, ValueError) as err:
            raise AgentError("cannot_connect") from err

    async def status(self):
        return await self.request("/status")

    async def set_enabled(self, enabled: bool):
        result = await self.request("/on" if enabled else "/off", "POST")
        if result["enabled"] is not enabled:
            raise AgentError("change_not_confirmed")
        return result
