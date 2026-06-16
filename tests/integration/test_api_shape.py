"""
End-to-end integration tests: every field the frontend reads from the API.

These tests hit the running services and assert the full response contract.
Any struct field drop, serialisation change, or SSE shape regression fails here.

Run (services must be running on localhost:8001 and localhost:8003):
    pytest tests/integration/test_api_shape.py -v

Token is read from the systemd service files automatically; override with:
    NZ_TENANCY_TOKEN=<tok> BUILDING_TOKEN=<tok> pytest ...
"""
import json
import os
import re
import time
from typing import Any

import pytest
import requests

# ---------------------------------------------------------------------------
# Endpoints and tokens
# ---------------------------------------------------------------------------

NZ_URL  = os.environ.get("NZ_TENANCY_URL", "http://localhost:8001")
BC_URL  = os.environ.get("BUILDING_URL",   "http://localhost:8003")

NO_LOG  = {"X-No-Log": "1"}


def _token_from_service(path: str) -> str:
    try:
        m = re.search(r"Environment=PUBLIC_TOKEN=(\S+)", open(path).read())
        return m.group(1) if m else ""
    except OSError:
        return ""


NZ_TOKEN = (os.environ.get("NZ_TENANCY_TOKEN")
            or _token_from_service(
               os.path.expanduser("~/.config/systemd/user/astraea-nz-tenancy.service")))

BC_TOKEN = (os.environ.get("BUILDING_TOKEN")
            or _token_from_service(
               os.path.expanduser("~/.config/systemd/user/buildingconsents-cpp.service")))


# ---------------------------------------------------------------------------
# Shared helpers
# ---------------------------------------------------------------------------

def _ask(base: str, token: str, question: str, **extra) -> dict:
    r = requests.post(f"{base}/ask",
                      json={"question": question, **extra},
                      headers={"X-API-Key": token, **NO_LOG},
                      timeout=120)
    assert r.status_code == 200, f"/ask {r.status_code}: {r.text[:300]}"
    return r.json()


def _stream_events(base: str, token: str, question: str, **extra) -> list[dict]:
    """Return all parsed SSE events from /ask/stream as a flat list."""
    r = requests.post(f"{base}/ask/stream",
                      json={"question": question, **extra},
                      headers={"X-API-Key": token, **NO_LOG},
                      stream=True,
                      timeout=120)
    assert r.status_code == 200, f"/ask/stream {r.status_code}"
    events = []
    for line in r.iter_lines():
        if isinstance(line, bytes):
            line = line.decode()
        line = line.strip()
        if line.startswith("data: "):
            try:
                events.append(json.loads(line[6:]))
            except json.JSONDecodeError:
                pass
    return events


def _events_of(events: list[dict], typ: str) -> list[dict]:
    return [e for e in events if e.get("type") == typ]


# ---------------------------------------------------------------------------
# Source-shape assertions (used in both /ask and /ask/stream)
#
# What the frontend (astraea.js renderSources) reads from each source:
#   s.url        -> href on the link (must be an https:// URL)
#   s.court_name -> part of the label text
#   s.date       -> part of the label text
#   s.id         -> S-index chip, feedback context
#   s.score      -> debug panel, context debug
#   s.label      -> pre-combined "court - date" string for non-JS consumers
# ---------------------------------------------------------------------------

def assert_source(src: dict, ctx: str) -> None:
    """
    Assert that a source dict matches the Python reference shape exactly:
      case_id, court_name, date, url
    This is the contract defined by Python core/pipeline.py public_sources.
    """
    # Mandatory: case reference from Qdrant payload (not the internal UUID)
    assert "case_id" in src and src["case_id"], \
        f"{ctx}: missing or empty 'case_id' (should be e.g. NZTT/2024/..., got keys: {list(src)})"

    # Mandatory for link label text in renderSources
    assert "court_name" in src, f"{ctx}: missing 'court_name'"
    assert isinstance(src["court_name"], str) and src["court_name"], \
        f"{ctx}: court_name must be a non-empty string"
    assert "date" in src, f"{ctx}: missing 'date'"
    assert isinstance(src["date"], str) and src["date"], \
        f"{ctx}: date must be a non-empty string"

    # Mandatory for links to render correctly
    assert "url" in src, f"{ctx}: missing 'url'"
    assert src["url"].startswith("https://"), \
        f"{ctx}: url must start with https://, got {src['url']!r}"

    # Verify no extra fields leaked into the public contract (parity with Python)
    extra = set(src.keys()) - {"case_id", "court_name", "date", "url"}
    assert not extra, \
        f"{ctx}: extra fields in source (not in Python contract): {extra}"


def assert_leg_source(leg: dict, ctx: str) -> None:
    """
    What the frontend reads from each legislation source:
      s.url     -> href on the link (legislation.govt.nz link)
      s.case_id -> section number extraction (/s123 pattern)
      s.title   -> link text (falls back to case_id)
    """
    assert "case_id" in leg and leg["case_id"], f"{ctx}: missing or empty 'case_id'"
    assert "title"   in leg,                    f"{ctx}: missing 'title'"
    assert "url"     in leg,                    f"{ctx}: missing 'url'"
    # url may be empty string (legislation with no URL stored) but must be present as a key
    if leg["url"]:
        assert leg["url"].startswith("https://"), \
            f"{ctx}: leg url must start with https://, got {leg['url']!r}"


def assert_confidence(ev: dict, ctx: str) -> None:
    """
    What the frontend reads from the confidence event (astraea.js renderConfidence):
      ev.level   -> CSS class, icon selection: "high" | "medium" | "low"
      ev.message -> text inside the badge
    """
    assert ev.get("type") == "confidence", f"{ctx}: type must be 'confidence'"
    assert ev.get("level") in ("high", "medium", "low"), \
        f"{ctx}: level must be high/medium/low, got {ev.get('level')!r}"
    assert "message" in ev and isinstance(ev["message"], str), \
        f"{ctx}: missing or non-string 'message'"
    assert "chunks"  in ev and isinstance(ev["chunks"], int), \
        f"{ctx}: missing or non-int 'chunks'"


def assert_token(ev: dict, ctx: str) -> None:
    assert ev.get("type") == "token", f"{ctx}: type must be 'token'"
    assert "text" in ev and isinstance(ev["text"], str), \
        f"{ctx}: token event must have string 'text'"


def assert_verification_section(sec: dict, ctx: str) -> None:
    """
    What renderVerification reads (nz_tenancy app.js):
      s.url       -> href on link
      s.reference -> link display text
      s.excerpt   -> content inside <pre>
    """
    assert "url" in sec,       f"{ctx}: missing 'url'"
    assert "reference" in sec, f"{ctx}: missing 'reference'"
    assert "excerpt" in sec,   f"{ctx}: missing 'excerpt'"
    if sec["url"]:
        assert sec["url"].startswith("https://"), \
            f"{ctx}: verification url must be https://, got {sec['url']!r}"
    assert sec["reference"], f"{ctx}: reference must be non-empty"


# ===========================================================================
# NZ Tenancy - /health and /healthz
# ===========================================================================

class TestNZTenancyHealth:
    def test_health_returns_200(self):
        r = requests.get(f"{NZ_URL}/health", timeout=10)
        assert r.status_code == 200

    def test_healthz_shape(self):
        r = requests.get(f"{NZ_URL}/healthz", timeout=15)
        assert r.status_code in (200, 503), f"unexpected status {r.status_code}"
        d = r.json()
        assert "status" in d, "healthz missing 'status'"
        assert "checks" in d and isinstance(d["checks"], list), \
            "healthz missing 'checks' list"
        for c in d["checks"]:
            for field in ("name", "status", "latency_ms"):
                assert field in c, f"healthz check missing '{field}': {c}"


# ===========================================================================
# NZ Tenancy - /ask (non-streaming)
# ===========================================================================

class TestNZTenancyAsk:
    Q = "What is the maximum bond a landlord can require?"

    def _d(self):
        return _ask(NZ_URL, NZ_TOKEN, self.Q)

    def test_answer_present(self):
        d = self._d()
        assert "answer" in d and d["answer"], "answer must be non-empty string"

    def test_sources_list_present(self):
        d = self._d()
        assert "sources" in d and isinstance(d["sources"], list) and d["sources"], \
            "sources must be a non-empty list"

    def test_source_fields(self):
        d = self._d()
        for i, src in enumerate(d["sources"]):
            assert_source(src, f"/ask sources[{i}]")

    def test_guidance_source_shape_when_present(self):
        d = self._d()
        gs = d.get("guidance_source")
        if gs is not None:
            assert_source(gs, "/ask guidance_source")


# ===========================================================================
# NZ Tenancy - /ask/stream (SSE)
# ===========================================================================

class TestNZTenancyStream:
    Q = "What is the maximum bond a landlord can require?"

    def _events(self):
        return _stream_events(NZ_URL, NZ_TOKEN, self.Q)

    # -- sources event -------------------------------------------------------

    def test_sources_event_present(self):
        assert _events_of(self._events(), "sources"), \
            "no 'sources' SSE event received"

    def test_sources_event_type_field(self):
        ev = _events_of(self._events(), "sources")[0]
        assert ev["type"] == "sources"

    def test_sources_event_has_sources_list(self):
        ev = _events_of(self._events(), "sources")[0]
        assert "sources" in ev and isinstance(ev["sources"], list), \
            "sources event must have 'sources' list"
        assert ev["sources"], "sources list must not be empty"

    def test_each_source_shape(self):
        ev = _events_of(self._events(), "sources")[0]
        for i, src in enumerate(ev["sources"]):
            assert_source(src, f"SSE sources[{i}]")

    def test_legislation_key_present(self):
        ev = _events_of(self._events(), "sources")[0]
        assert "legislation" in ev and isinstance(ev["legislation"], list), \
            "sources event must have 'legislation' list (may be empty)"

    def test_each_leg_source_shape(self):
        ev = _events_of(self._events(), "sources")[0]
        for i, leg in enumerate(ev.get("legislation", [])):
            assert_leg_source(leg, f"SSE legislation[{i}]")

    # -- confidence event ----------------------------------------------------

    def test_confidence_event_present(self):
        assert _events_of(self._events(), "confidence"), \
            "no 'confidence' SSE event received"

    def test_confidence_event_shape(self):
        ev = _events_of(self._events(), "confidence")[0]
        assert_confidence(ev, "SSE confidence")

    # -- token events --------------------------------------------------------

    def test_token_events_present(self):
        events = self._events()
        tok = _events_of(events, "token")
        assert tok, "no 'token' events - answer is empty"

    def test_token_event_shape(self):
        events = self._events()
        for i, ev in enumerate(_events_of(events, "token")[:5]):
            assert_token(ev, f"SSE token[{i}]")

    # -- done event ----------------------------------------------------------

    def test_done_event_present(self):
        assert _events_of(self._events(), "done"), \
            "no 'done' SSE event - stream never finished"

    # -- verification event (optional but shape must be correct if present) --

    def test_verification_sections_shape_when_present(self):
        events = self._events()
        vev = _events_of(events, "verification")
        if not vev:
            pytest.skip("no verification event in this response")
        for i, sec in enumerate(vev[0].get("sections", [])):
            assert_verification_section(sec, f"SSE verification.sections[{i}]")

    # -- ordering invariant --------------------------------------------------

    def test_sources_before_tokens(self):
        events = self._events()
        types = [e.get("type") for e in events]
        sources_idx = next((i for i, t in enumerate(types) if t == "sources"), None)
        token_idx   = next((i for i, t in enumerate(types) if t == "token"),   None)
        if sources_idx is not None and token_idx is not None:
            assert sources_idx < token_idx, \
                "sources event must arrive before first token"

    def test_done_is_last_significant_event(self):
        events = self._events()
        types = [e.get("type") for e in events]
        done_idx = next((i for i, t in enumerate(types) if t == "done"), None)
        assert done_idx is not None
        after_done = [t for t in types[done_idx + 1:] if t not in (None,)]
        assert not after_done, \
            f"unexpected events after 'done': {after_done}"


# ===========================================================================
# Building Consents - /health
# ===========================================================================

class TestBuildingConsentsHealth:
    def test_health_returns_200(self):
        r = requests.get(f"{BC_URL}/health", timeout=10)
        assert r.status_code == 200


# ===========================================================================
# Building Consents - /ask (non-streaming)
# ===========================================================================

class TestBuildingConsentsAsk:
    Q = "Do I need a building consent for a carport?"

    def _d(self):
        return _ask(BC_URL, BC_TOKEN, self.Q)

    def test_answer_present(self):
        d = self._d()
        assert "answer" in d and d["answer"], "answer must be non-empty string"

    def test_sources_list_present(self):
        d = self._d()
        assert "sources" in d and isinstance(d["sources"], list), \
            "sources must be a list"

    def test_source_fields(self):
        d = self._d()
        for i, src in enumerate(d["sources"]):
            assert_source(src, f"building /ask sources[{i}]")


# ===========================================================================
# Building Consents - /ask/stream (SSE)
# ===========================================================================

class TestBuildingConsentsStream:
    Q = "Do I need a building consent for a carport?"

    def _events(self):
        return _stream_events(BC_URL, BC_TOKEN, self.Q)

    def test_sources_event_present(self):
        assert _events_of(self._events(), "sources"), \
            "no 'sources' SSE event"

    def test_source_fields(self):
        ev = _events_of(self._events(), "sources")[0]
        for i, src in enumerate(ev.get("sources", [])):
            assert_source(src, f"building SSE sources[{i}]")

    def test_confidence_event_shape(self):
        evs = _events_of(self._events(), "confidence")
        assert evs, "no 'confidence' SSE event"
        assert_confidence(evs[0], "building SSE confidence")

    def test_token_events_present(self):
        assert _events_of(self._events(), "token"), \
            "no 'token' events"

    def test_done_event_present(self):
        assert _events_of(self._events(), "done"), \
            "no 'done' SSE event"


# ===========================================================================
# Building Consents - /zone
#
# What lookupZone() in app.js reads:
#   data.found          -> branch on whether zone was found
#   data.zone           -> nested object (must exist when found=true)
#   data.zone.zone_name -> displayed in the badge
#   data.zone.zone_code -> displayed in the badge
#   data.zone.council   -> displayed in the badge
#   data.lat / data.lng -> available (not currently displayed but in contract)
# ===========================================================================

class TestZoneEndpoint:
    HEADERS = {"X-API-Key": BC_TOKEN, **NO_LOG}
    ADDRESS_KNOWN   = "14 Kelvin Road, Papakura, Auckland"
    ADDRESS_UNKNOWN = "1 Nowhere Street, Mars"

    def _zone(self, address: str) -> dict:
        r = requests.post(f"{BC_URL}/zone",
                          json={"address": address},
                          headers=self.HEADERS,
                          timeout=30)
        assert r.status_code == 200, f"/zone {r.status_code}: {r.text[:200]}"
        return r.json()

    def test_found_has_required_keys(self):
        d = self._zone(self.ADDRESS_KNOWN)
        assert "found" in d, "zone response missing 'found'"

    def test_found_true_has_nested_zone(self):
        d = self._zone(self.ADDRESS_KNOWN)
        if not d.get("found"):
            pytest.skip("known address not resolved - geocode sidecar may be down")
        assert "zone" in d, \
            "zone response missing nested 'zone' object when found=true"
        zone = d["zone"]
        for field in ("zone_code", "zone_name", "council"):
            assert field in zone and zone[field], \
                f"zone.{field} missing or empty: {zone}"

    def test_found_true_has_lat_lng(self):
        d = self._zone(self.ADDRESS_KNOWN)
        if not d.get("found"):
            pytest.skip("known address not resolved")
        assert "lat" in d and isinstance(d["lat"], float), \
            "zone response missing float 'lat'"
        assert "lng" in d and isinstance(d["lng"], float), \
            "zone response missing float 'lng'"

    def test_not_found_shape(self):
        d = self._zone(self.ADDRESS_UNKNOWN)
        assert "found" in d
        if not d["found"]:
            # When not found, zone key must NOT be set (or must be null/absent)
            zone = d.get("zone")
            assert not zone, \
                f"zone must be absent/null when found=false, got {zone!r}"

    def test_zone_name_is_displayed_string(self):
        d = self._zone(self.ADDRESS_KNOWN)
        if not d.get("found"):
            pytest.skip("known address not resolved")
        assert isinstance(d["zone"]["zone_name"], str) and d["zone"]["zone_name"], \
            "zone_name must be a non-empty string (shown in badge)"

    def test_error_field_present(self):
        d = self._zone(self.ADDRESS_KNOWN)
        # error key must always be present (empty string on success)
        assert "error" in d, "zone response must always have 'error' key"


# ===========================================================================
# Python vs C++ parity - NZ Tenancy
#
# The Python reference service (port 8000, no auth) defines the canonical
# source contract. These tests confirm that C++ (port 8001) emits the same
# field names per source: case_id, court_name, date, url.
#
# Actual retrieved documents differ between the two services (different
# retrieval paths, scoring, etc.), so we only compare field names and
# structural shapes, not values.
# ===========================================================================

PY_URL = os.environ.get("PY_NZ_URL", "http://127.0.0.1:8000")
PARITY_Q = "What is the maximum bond a landlord can require?"

# Python fields defined in core/pipeline.py public_sources
PYTHON_SOURCE_KEYS = {"case_id", "court_name", "date", "url"}


def _py_stream_events(question: str) -> list[dict]:
    """Stream events from the Python reference service (no auth required)."""
    try:
        r = requests.post(f"{PY_URL}/ask/stream",
                          json={"question": question},
                          headers=NO_LOG,
                          stream=True,
                          timeout=120)
    except requests.ConnectionError:
        pytest.skip(f"Python reference service not reachable at {PY_URL}")
    if r.status_code == 404:
        pytest.skip(f"Python service at {PY_URL} returned 404 - wrong URL")
    assert r.status_code == 200, f"Python /ask/stream {r.status_code}"
    events = []
    for line in r.iter_lines():
        if isinstance(line, bytes):
            line = line.decode()
        line = line.strip()
        if line.startswith("data: "):
            try:
                events.append(json.loads(line[6:]))
            except json.JSONDecodeError:
                pass
    return events


class TestNZTenancyPythonCppParity:
    """
    Verifies that the C++ /ask/stream response matches Python's contract.
    Both services must be running. If the Python service is unreachable the
    tests are automatically skipped (not failed) so CI is not blocked when
    only the C++ service is deployed.
    """

    def _py_sources(self) -> list[dict]:
        evs = _py_stream_events(PARITY_Q)
        src_evs = [e for e in evs if e.get("type") == "sources"]
        assert src_evs, "Python service returned no 'sources' SSE event"
        return src_evs[0].get("sources", [])

    def _cpp_sources(self) -> list[dict]:
        evs = _stream_events(NZ_URL, NZ_TOKEN, PARITY_Q)
        src_evs = [e for e in evs if e.get("type") == "sources"]
        assert src_evs, "C++ service returned no 'sources' SSE event"
        return src_evs[0].get("sources", [])

    def test_python_source_keys_match_contract(self):
        """Python service itself returns exactly the 4 expected fields."""
        srcs = self._py_sources()
        assert srcs, "Python returned an empty sources list"
        for i, src in enumerate(srcs):
            actual = set(src.keys())
            assert actual == PYTHON_SOURCE_KEYS, \
                f"Python source[{i}] keys {actual} != expected {PYTHON_SOURCE_KEYS}"

    def test_cpp_source_keys_match_python_contract(self):
        """C++ service emits exactly the same fields as Python - no more, no less."""
        srcs = self._cpp_sources()
        assert srcs, "C++ returned an empty sources list"
        for i, src in enumerate(srcs):
            actual = set(src.keys())
            assert actual == PYTHON_SOURCE_KEYS, \
                f"C++ source[{i}] keys {actual} != Python contract {PYTHON_SOURCE_KEYS}"

    def test_cpp_sources_have_valid_urls(self):
        """C++ case_id is a proper case reference, url is a real https:// link."""
        for i, src in enumerate(self._cpp_sources()):
            assert src["case_id"], f"C++ source[{i}]: empty case_id"
            assert src["url"].startswith("https://"), \
                f"C++ source[{i}]: url must be https://, got {src['url']!r}"

    def test_both_have_confidence_event_same_shape(self):
        """Both services emit a confidence event with the same field structure."""
        py_evs  = _py_stream_events(PARITY_Q)
        cpp_evs = _stream_events(NZ_URL, NZ_TOKEN, PARITY_Q)
        py_conf  = [e for e in py_evs  if e.get("type") == "confidence"]
        cpp_conf = [e for e in cpp_evs if e.get("type") == "confidence"]
        assert py_conf,  "Python returned no 'confidence' event"
        assert cpp_conf, "C++ returned no 'confidence' event"
        for key in ("level", "chunks", "message"):
            assert key in py_conf[0],  f"Python confidence missing '{key}'"
            assert key in cpp_conf[0], f"C++ confidence missing '{key}'"
        assert py_conf[0]["level"]  in ("high", "medium", "low"), \
            f"Python confidence level invalid: {py_conf[0]['level']!r}"
        assert cpp_conf[0]["level"] in ("high", "medium", "low"), \
            f"C++ confidence level invalid: {cpp_conf[0]['level']!r}"
