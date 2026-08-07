#!/usr/bin/env python3
def altitude_from_pressures(station_hpa, sea_level_hpa, temp_c):
    return ((sea_level_hpa / station_hpa) ** (1.0 / 5.257) - 1.0) * (temp_c + 273.15) / 0.0065

def sea_level_pressure(station_hpa, altitude_m, temp_c):
    denominator = temp_c + 0.0065 * altitude_m + 273.15
    base = 1.0 - (0.0065 * altitude_m) / denominator
    return station_hpa * base ** -5.257

h = altitude_from_pressures(974.0, 1014.0, 15.0)
assert 340.0 < h < 342.0, h
p0 = sea_level_pressure(974.0, h, 15.0)
assert abs(p0 - 1014.0) < 0.01, p0
h_warm = altitude_from_pressures(974.0, 1014.0, 25.0)
assert 352.0 < h_warm < 354.0, h_warm
print(f"ALTITUDE CALIBRATION TEST OK: {h:.1f} m at 15 C, {h_warm:.1f} m at 25 C")
