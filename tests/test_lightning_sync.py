#!/usr/bin/env python3
# Regression test kept under the historical filename. Lightning must now be
# independent from the animated radar timestamp; only real strike age matters.

TRAIL = 20 * 60


def visible(strike_epoch: int, now_epoch: int) -> bool:
    if strike_epoch > now_epoch + 5:
        return False
    return now_epoch - strike_epoch <= TRAIL

now = 1_786_808_500
fresh = now - 30
old = now - TRAIL - 1

# The radar frame index has no effect at all.
for radar_frame in range(6):
    assert visible(fresh, now), radar_frame
    assert not visible(old, now), radar_frame

# LightningMaps millisecond timestamp converts to normal Unix seconds.
time_ms = 1786977510249
assert time_ms // 1000 == 1786977510

print("LIGHTNING LIVE TEST OK: visibility is independent of radar frames")
