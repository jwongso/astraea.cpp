"""
Integration tests: verify the JSON response shape of the live C++ API.

These tests hit the running services directly and assert that every field
the frontend needs is present and has the right type. A missing field here
means the frontend will silently break.

Run (services must be running):
    pytest tests/integration/test_api_shape.py -v

Required env vars:
    NZ_TENANCY_TOKEN   - PUBLIC_TOKEN for port 8001 (or falls back to
                         parsing the systemd service file)
    BUILDING_TOKEN     - PUBLIC_TOKEN for port 8003 (same fallback)
"""
import json
import os
import re
import subprocess
import sys
import time
from typing import Any

import pytest
import requests

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

NZ_TENANCY_URL = os.environ.get("NZ_TENANCY_URL", "http://localhost:8001")
BUILDING_URL   = os.environ.get("BUILDING_URL",   "http://localhost:8003")

NO_LOG_HEADERS = {"X-No-Log": "1"}


def _token_from_service(service_file: str) -> str:
    try:
        content = open(service_file).read()
        m = re.search(r"Environment=PUBLIC_TOKEN=(\S+)", content)
        if m:
            return m.group(1)
    except OSError:
        pass
    return ""


def _nz_token() -> str:
    return (os.environ.get("NZ_TENANCY_TOKEN")
            or _token_from_service(
                os.path.expanduser("~/.config/systemd/user/astraea-nz-tenancy.service")))


def _bc_token() -> str:
    return (os.environ.get("BUILDING_TOKEN")
            or _token_from_service(
                os.path.expanduser("~/.config/systemd/user/buildingconsents-cpp.service")))


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

SOURCE_REQUIRED_FIELDS = {"id", "score", "label", "url"}

def assert_source_shape(src: dict, ctx: str) -> None:
    missing = SOURCE_REQUIRED_FIELDS - src.keys()
    assert not missing, f"{ctx}: source missing fields {missing}: {src}"
    assert isinstance(src["id"],    str),   f"{ctx}: source.id must be str"
    assert isinstance(src["score"], float), f"{ctx}: source.score must be float"
    assert isinstance(src["label"], str),   f"{ctx}: source.label must be str"
    assert isinstance(src["url"],   str),   f"{ctx}: source.url must be str"
    assert src["id"],                       f"{ctx}: source.id must be non-empty"
    assert src["label"],                    f"{ctx}: source.label must be non-empty"
    assert src["url"].startswith("http"),   f"{ctx}: source.url must be an http URL, got {src['url']!r}"


def parse_sse_stream(raw: bytes) -> list[dict]:
    events = []
    for line in raw.decode().splitlines():
        line = line.strip()
        if line.startswith("data: "):
            try:
                events.append(json.loads(line[6:]))
            except json.JSONDecodeError:
                pass
    return events


# ---------------------------------------------------------------------------
# NZ Tenancy - /ask (non-streaming)
# ---------------------------------------------------------------------------

class TestNZTenancyAsk:
    TOKEN = _nz_token()
    HEADERS = {"X-API-Key": TOKEN, **NO_LOG_HEADERS}
    QUESTION = "What is the maximum bond a landlord can require?"

    def _ask(self, question: str = QUESTION) -> dict:
        resp = requests.post(
            f"{NZ_TENANCY_URL}/ask",
            json={"question": question},
            headers=self.HEADERS,
            timeout=90,
        )
        assert resp.status_code == 200, f"/ask returned {resp.status_code}: {resp.text[:200]}"
        return resp.json()

    def test_response_has_answer(self):
        d = self._ask()
        assert "answer" in d, "response missing 'answer'"
        assert isinstance(d["answer"], str) and d["answer"], "answer must be non-empty string"

    def test_response_has_sources(self):
        d = self._ask()
        assert "sources" in d, "response missing 'sources'"
        assert isinstance(d["sources"], list), "sources must be a list"
        assert len(d["sources"]) > 0, "sources must not be empty"

    def test_source_fields_complete(self):
        d = self._ask()
        for i, src in enumerate(d["sources"]):
            assert_source_shape(src, f"/ask source[{i}]")

    def test_health_endpoint(self):
        resp = requests.get(f"{NZ_TENANCY_URL}/health", timeout=10)
        assert resp.status_code == 200


# ---------------------------------------------------------------------------
# NZ Tenancy - /ask/stream (SSE)
# ---------------------------------------------------------------------------

class TestNZTenancyStream:
    TOKEN = _nz_token()
    HEADERS = {"X-API-Key": TOKEN, **NO_LOG_HEADERS}
    QUESTION = "What is the maximum bond a landlord can require?"

    def _stream(self, question: str = QUESTION) -> list[dict]:
        resp = requests.post(
            f"{NZ_TENANCY_URL}/ask/stream",
            json={"question": question},
            headers=self.HEADERS,
            stream=True,
            timeout=120,
        )
        assert resp.status_code == 200, f"/ask/stream returned {resp.status_code}"
        return parse_sse_stream(resp.content)

    def test_sources_event_present(self):
        events = self._stream()
        sources_events = [e for e in events if e.get("type") == "sources"]
        assert sources_events, "no 'sources' SSE event received"

    def test_sources_event_fields(self):
        events = self._stream()
        sources_events = [e for e in events if e.get("type") == "sources"]
        assert sources_events
        ev = sources_events[0]
        assert "sources" in ev,     "sources event missing 'sources' key"
        assert isinstance(ev["sources"], list)
        assert len(ev["sources"]) > 0, "sources event has empty sources list"
        for i, src in enumerate(ev["sources"]):
            assert_source_shape(src, f"SSE sources[{i}]")

    def test_confidence_event_present(self):
        events = self._stream()
        conf_events = [e for e in events if e.get("type") == "confidence"]
        assert conf_events, "no 'confidence' SSE event received"
        ev = conf_events[0]
        assert "level"   in ev, "confidence event missing 'level'"
        assert "chunks"  in ev, "confidence event missing 'chunks'"
        assert "message" in ev, "confidence event missing 'message'"
        assert ev["level"] in ("high", "medium", "low"), \
            f"unexpected confidence level: {ev['level']!r}"

    def test_token_events_present(self):
        events = self._stream()
        token_events = [e for e in events if e.get("type") == "token"]
        assert token_events, "no 'token' SSE events received (empty answer)"
        for e in token_events:
            assert "text" in e, f"token event missing 'text': {e}"

    def test_done_event_present(self):
        events = self._stream()
        done_events = [e for e in events if e.get("type") == "done"]
        assert done_events, "no 'done' SSE event received (stream never closed)"


# ---------------------------------------------------------------------------
# Building Consents - /ask (non-streaming)
# ---------------------------------------------------------------------------

class TestBuildingConsentsAsk:
    TOKEN = _bc_token()
    HEADERS = {"X-API-Key": TOKEN, **NO_LOG_HEADERS}
    QUESTION = "Do I need a consent to build a carport?"

    def _ask(self, question: str = QUESTION) -> dict:
        resp = requests.post(
            f"{BUILDING_URL}/ask",
            json={"question": question},
            headers=self.HEADERS,
            timeout=90,
        )
        assert resp.status_code == 200, f"/ask returned {resp.status_code}: {resp.text[:200]}"
        return resp.json()

    def test_response_has_answer(self):
        d = self._ask()
        assert "answer" in d and d["answer"], "answer must be non-empty string"

    def test_source_fields_complete(self):
        d = self._ask()
        for i, src in enumerate(d.get("sources", [])):
            assert_source_shape(src, f"building_consents /ask source[{i}]")

    def test_zone_endpoint(self):
        resp = requests.post(
            f"{BUILDING_URL}/zone",
            json={"address": "14 Kelvin Road, Papakura"},
            headers={"X-API-Key": self.TOKEN, **NO_LOG_HEADERS},
            timeout=30,
        )
        assert resp.status_code == 200
        d = resp.json()
        assert "found" in d, "zone response missing 'found'"
        if d["found"]:
            assert "zone" in d, "zone response missing nested 'zone' object when found=true"
            zone = d["zone"]
            for field in ("zone_code", "zone_name", "council"):
                assert field in zone, f"zone object missing '{field}'"
            assert "lat" in d and "lng" in d, "zone response missing lat/lng"
