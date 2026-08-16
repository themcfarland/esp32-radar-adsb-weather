#!/usr/bin/env python3
from datetime import datetime, timezone, timedelta

STEP = timedelta(minutes=5)


def in_frame_slot(strike: datetime, radar_end: datetime) -> bool:
    return radar_end - STEP < strike <= radar_end

radar_end = datetime(2026, 8, 15, 15, 0, tzinfo=timezone.utc)
assert in_frame_slot(datetime(2026, 8, 15, 15, 0, 0, tzinfo=timezone.utc), radar_end)
assert in_frame_slot(datetime(2026, 8, 15, 14, 55, 1, tzinfo=timezone.utc), radar_end)
assert not in_frame_slot(datetime(2026, 8, 15, 14, 55, 0, tzinfo=timezone.utc), radar_end)
assert not in_frame_slot(datetime(2026, 8, 15, 14, 54, 59, tzinfo=timezone.utc), radar_end)
assert not in_frame_slot(datetime(2026, 8, 15, 15, 0, 1, tzinfo=timezone.utc), radar_end)

# Adjacent radar frames must not display the same strike.
next_radar_end = radar_end + STEP
strike = radar_end
assert in_frame_slot(strike, radar_end)
assert not in_frame_slot(strike, next_radar_end)

# The sample nanosecond timestamp format converts to normal Unix seconds.
time_ns = 1786808428328945400
assert time_ns // 1_000_000_000 == 1786808428

print("LIGHTNING SYNC TEST OK: each strike belongs to one 5-minute radar frame")
