#!/usr/bin/env python3
"""Reference compensation check using the BMP180 datasheet example."""

cal = {
    "ac1": 408,
    "ac2": -72,
    "ac3": -14383,
    "ac4": 32741,
    "ac5": 32757,
    "ac6": 23153,
    "b1": 6190,
    "b2": 4,
    "mc": -8711,
    "md": 2868,
}


def compensate(ut: int, up: int, oss: int) -> tuple[float, int]:
    x1t = ((ut - cal["ac6"]) * cal["ac5"]) >> 15
    x2t = (cal["mc"] << 11) // (x1t + cal["md"])
    b5 = x1t + x2t
    temperature_c = ((b5 + 8) >> 4) / 10.0

    b6 = b5 - 4000
    x1 = (cal["b2"] * ((b6 * b6) >> 12)) >> 11
    x2 = (cal["ac2"] * b6) >> 11
    x3 = x1 + x2
    b3 = ((((cal["ac1"] * 4 + x3) << oss) + 2) >> 2)
    x1 = (cal["ac3"] * b6) >> 13
    x2 = (cal["b1"] * ((b6 * b6) >> 12)) >> 16
    x3 = (x1 + x2 + 2) >> 2
    b4 = (cal["ac4"] * (x3 + 32768)) >> 15
    b7 = (up - b3) * (50000 >> oss)
    pressure_pa = (b7 * 2) // b4 if b7 < 0x80000000 else (b7 // b4) * 2
    x1 = pressure_pa >> 8
    x1 = (x1 * x1 * 3038) >> 16
    x2 = (-7357 * pressure_pa) >> 16
    pressure_pa += (x1 + x2 + 3791) >> 4
    return temperature_c, pressure_pa


temperature, pressure = compensate(27898, 23843, 0)
assert temperature == 15.0, temperature
assert pressure == 69964, pressure
print("BMP180 REFERENCE TEST OK: 15.0 C, 69964 Pa")
