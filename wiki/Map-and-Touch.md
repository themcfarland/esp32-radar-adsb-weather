# Mapa a dotykové ovládání

## Režimy výřezu

Každé klepnutí do mapy přepne další režim:

```text
celá ČR → 50 km → 25 km → 10 km → celá ČR
```

Výřez se při přiblížení centruje na místo dotyku. Při návratu na celou ČR se obnoví pevný celostátní rozsah.

## Uložení do NVS

Namespace:

```text
mapview
```

Ukládané hodnoty:

```text
mode
lat
lon
```

Zápis se provede až po krátké prodlevě od posledního klepnutí. Tím se omezuje opotřebení flash paměti.

## Společný viewport

Stejné geografické hranice používají:

- radar,
- hranice České republiky,
- města,
- stanice Dolní Vlkys,
- letadla ADS-B,
- geografická síť.

## Obrys České republiky

Obrys pochází z geoBoundaries CZE ADM0 a je redukován na 300 bodů. Zdrojový zjednodušený GeoJSON je uložen v:

```text
data/czech_border_300.geojson
```
