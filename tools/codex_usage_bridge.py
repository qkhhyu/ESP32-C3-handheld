#!/usr/bin/env python3
"""Minimal LAN bridge for the ESP32 Codex usage card.

It reads the current local Codex OAuth login, asks ChatGPT for quota, then exposes
only percentages and reset countdowns.  The OAuth token never leaves this PC.

Example (PowerShell):
  python tools/codex_usage_bridge.py --token change-this-to-a-long-random-string
"""

from __future__ import annotations

import argparse
import base64
import hmac
import json
import os
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


USAGE_URL = "https://chatgpt.com/backend-api/wham/usage"
CACHE_SECONDS = 30


def find_value(value: Any, name: str) -> str | None:
    """Find a string field across auth.json layouts used by Codex releases."""
    if isinstance(value, dict):
        candidate = value.get(name)
        if isinstance(candidate, str) and candidate:
            return candidate
        for child in value.values():
            found = find_value(child, name)
            if found:
                return found
    elif isinstance(value, list):
        for child in value:
            found = find_value(child, name)
            if found:
                return found
    return None


def jwt_account_id(id_token: str) -> str | None:
    try:
        payload = id_token.split(".")[1]
        payload += "=" * (-len(payload) % 4)
        claims = json.loads(base64.urlsafe_b64decode(payload))
        auth = claims.get("https://api.openai.com/auth", {})
        value = auth.get("chatgpt_account_id")
        return value if isinstance(value, str) and value else None
    except (IndexError, ValueError, UnicodeDecodeError, json.JSONDecodeError):
        return None


def read_auth(auth_file: Path) -> tuple[str, str]:
    data = json.loads(auth_file.read_text(encoding="utf-8"))
    access_token = find_value(data, "access_token")
    account_id = find_value(data, "account_id")
    if not account_id:
        id_token = find_value(data, "id_token")
        account_id = jwt_account_id(id_token) if id_token else None
    if not access_token or not account_id:
        raise RuntimeError("Codex auth.json has no usable access_token/account_id; run codex login")
    return access_token, account_id


def quota_window(value: Any) -> dict[str, int] | None:
    if not isinstance(value, dict) or not isinstance(value.get("used_percent"), (int, float)):
        return None
    result = {"used_percent": max(0, min(100, round(value["used_percent"])))}
    if isinstance(value.get("reset_after_seconds"), (int, float)):
        result["reset_after_seconds"] = max(0, round(value["reset_after_seconds"]))
    return result


class UsageSource:
    def __init__(self, auth_file: Path) -> None:
        self.auth_file = auth_file
        self.lock = threading.Lock()
        self.cached_at = 0.0
        self.cached: dict[str, Any] | None = None

    def fetch(self) -> dict[str, Any]:
        with self.lock:
            if self.cached and time.monotonic() - self.cached_at < CACHE_SECONDS:
                return self.cached

            access_token, account_id = read_auth(self.auth_file)
            request = Request(
                USAGE_URL,
                headers={
                    "Authorization": f"Bearer {access_token}",
                    "ChatGPT-Account-ID": account_id,
                    "originator": "codex_cli_rs",
                    "Accept": "application/json",
                    "Accept-Encoding": "identity",
                    "User-Agent": "esp32-codex-usage-bridge/1.0",
                },
            )
            with urlopen(request, timeout=15) as response:
                data = json.load(response)

            rate_limit = data.get("rate_limit", {})
            primary = quota_window(rate_limit.get("primary_window"))
            if primary is None:
                raise RuntimeError("usage endpoint returned no primary_window")
            result: dict[str, Any] = {"primary": primary}
            secondary = quota_window(rate_limit.get("secondary_window"))
            if secondary is not None:
                result["secondary"] = secondary
            plan = data.get("plan_type")
            if isinstance(plan, str):
                result["plan"] = plan
            self.cached = result
            self.cached_at = time.monotonic()
            return result


def make_handler(source: UsageSource, token: str):
    class Handler(BaseHTTPRequestHandler):
        def log_message(self, _format: str, *_args: object) -> None:
            # Deliberately avoid logging request headers, especially the LAN token.
            return

        def send_json(self, status: int, data: dict[str, Any]) -> None:
            body = json.dumps(data, separators=(",", ":")).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:  # noqa: N802 - BaseHTTPRequestHandler API
            if self.path != "/usage":
                self.send_json(404, {"error": "not_found"})
                return
            supplied = self.headers.get("X-Usage-Token", "")
            if not hmac.compare_digest(supplied, token):
                self.send_json(401, {"error": "unauthorized"})
                return
            try:
                self.send_json(200, source.fetch())
            except (OSError, ValueError, HTTPError, URLError, RuntimeError) as exc:
                # Do not return OAuth contents or upstream response bodies to the LAN.
                self.send_json(502, {"error": "upstream_unavailable", "detail": str(exc)[:120]})

    return Handler


def main() -> None:
    default_auth = Path(os.environ.get("CODEX_HOME", Path.home() / ".codex")) / "auth.json"
    parser = argparse.ArgumentParser(description="Expose sanitized Codex usage on the LAN")
    parser.add_argument("--token", required=True, help="long random LAN token expected in X-Usage-Token")
    parser.add_argument("--auth-file", type=Path, default=default_auth)
    parser.add_argument("--bind", default="0.0.0.0", help="LAN bind address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8787)
    args = parser.parse_args()
    if len(args.token) < 16:
        parser.error("--token must be at least 16 characters")
    if not args.auth_file.is_file():
        parser.error(f"Codex auth file not found: {args.auth_file}")

    server = ThreadingHTTPServer((args.bind, args.port), make_handler(UsageSource(args.auth_file), args.token))
    print(f"Codex usage bridge listening on http://{args.bind}:{args.port}/usage")
    print("The endpoint reveals quota only; Ctrl+C stops it.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
