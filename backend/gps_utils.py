"""GPS and navigation helpers for telemetry enrichment."""

from __future__ import annotations

import math
from typing import Optional

EARTH_RADIUS_M = 6_371_000.0


def haversine(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    """Return the great-circle distance in metres between two coordinates."""
    phi1 = math.radians(lat1)
    phi2 = math.radians(lat2)
    d_phi = math.radians(lat2 - lat1)
    d_lambda = math.radians(lon2 - lon1)

    a = math.sin(d_phi / 2.0) ** 2 + math.cos(phi1) * math.cos(phi2) * math.sin(d_lambda / 2.0) ** 2
    return 2.0 * EARTH_RADIUS_M * math.atan2(math.sqrt(a), math.sqrt(1.0 - a))


def fix_type_str(code: int) -> str:
    """Map a numeric GPS fix code to a display string."""
    mapping = {0: "NO_FIX", 1: "2D", 2: "3D"}
    return mapping.get(code, "UNKNOWN")


def compute_descent_rate(alt_now: float, alt_prev: Optional[float], dt: float) -> float:
    """Compute signed vertical speed in m/s."""
    if dt == 0 or alt_prev is None:
        return 0.0
    return (alt_now - alt_prev) / dt
