"""
Path setup and FastAPI stub for the differential parity tests.

core/sanitize.py imports FastAPI (available only in the project venv, not
system Python 3.14). We stub it here so the module loads cleanly; the stub
HTTPException is a real exception so raise/except still works correctly.
"""
import sys
import pathlib
from unittest.mock import MagicMock

# --- FastAPI stub (must come before any core.* import) ---
if "fastapi" not in sys.modules:
    _fastapi = MagicMock()

    class _HTTPException(Exception):
        def __init__(self, status_code: int = 400, detail=None):
            self.status_code = status_code
            self.detail = detail
            super().__init__(str(detail) if detail else "")

    _fastapi.HTTPException = _HTTPException
    sys.modules["fastapi"] = _fastapi
    sys.modules["fastapi.responses"] = MagicMock()
    sys.modules["fastapi.middleware"] = MagicMock()
    sys.modules["fastapi.middleware.cors"] = MagicMock()

# --- Python paths ---
_root = pathlib.Path(__file__).parent.parent.parent
_build = _root / "build-prod"
_astraea_py = pathlib.Path("/home/wdha/proj/priv/astraea")

for p in [str(_build), str(_astraea_py)]:
    if p not in sys.path:
        sys.path.insert(0, p)
