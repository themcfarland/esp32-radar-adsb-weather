#!/usr/bin/env python3
import math

EARTH_KM = 6371.0088
HOME_LAT = 49.8175
HOME_LON = 15.4730

def distance_km(lat1, lon1, lat2, lon2):
    lat1r = math.radians(lat1)
    lat2r = math.radians(lat2)
    dlat = math.radians(lat2-lat1)
    dlon = math.radians(lon2-lon1)
    a = math.sin(dlat/2)**2 + math.cos(lat1r)*math.cos(lat2r)*math.sin(dlon/2)**2
    a = max(0.0, min(1.0, a))
    return 2*EARTH_KM*math.atan2(math.sqrt(a), math.sqrt(1-a))

def destination_north(distance):
    return HOME_LAT + math.degrees(distance/EARTH_KM), HOME_LON

lat5, lon5 = destination_north(5.0)
lat10, lon10 = destination_north(10.0)
lat11, lon11 = destination_north(11.0)
assert distance_km(HOME_LAT, HOME_LON, lat5, lon5) < 10.0
assert abs(distance_km(HOME_LAT, HOME_LON, lat10, lon10) - 10.0) < 0.01
assert distance_km(HOME_LAT, HOME_LON, lat11, lon11) > 10.0
print("LIGHTNING PROXIMITY TEST OK: 5 km inside, 10 km boundary, 11 km outside")
