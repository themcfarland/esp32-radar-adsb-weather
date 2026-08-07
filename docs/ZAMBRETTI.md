# Zambretti v projektu

Firmware odděluje dvě různé informace:

1. **Internetová numerická předpověď** Open-Meteo/WU pro +3, +6 a +9 hodin.
2. **Lokální kategoriální předpověď Zambretti** z barometru, tříhodinového trendu, ročního období a volitelného směru větru.

Zambretti neurčuje samostatný tlak ani počasí přesně pro +3, +6 a +9 hodin. Výstupem je jeden z kódů A-Z a odpovídající krátký popis očekávaného vývoje.

## Vstupní tlak

Tlak senzoru se přepočítává na hladinu moře s využitím klouzavého průměru venkovní teploty Weather Underground za posledních nejvýše 12 hodin a nadmořské výšky nastavené ve webu:

```text
P0 = P * (1 - 0.0065*h / (T + 0.0065*h + 273.15))^(-5.257) + offset
```

- `P` je tlak senzoru v hPa,
- `h` je nadmořská výška v metrech,
- `T` je venkovní teplota z WU, zprůměrovaná v až 12hodinovém RAM okně,
- `offset` je uživatelská kalibrační korekce.

Stejná WU observation se podle pole `epoch` nepřidává vícekrát a při změně ID WU stanice se teplotní historie vymaže. Chybí-li WU teplota nebo je-li poslední záznam starší než 12 hodin, použije se standardních 15 °C. Vlastní teplota barometru se používá pouze jako diagnostický údaj. Při prvním přechodu z náhradních 15 °C na platnou WU teplotu se tlaková historie znovu založí, aby graf nemíchal dva různé převody.

Trend se počítá z přímo naměřeného staničního tlaku před redukcí na hladinu moře. Změny venkovní teploty proto nemohou vytvořit falešný vzestup nebo pokles tlaku. Historie pro graf současně uchovává redukovaný tlak u hladiny moře.

## Historie a trend

- senzor se čte každou minutu,
- do RAM historie se přijme jeden pevný bod po pěti minutách,
- časová značka přijatého bodu se později neposouvá,
- trend je směrnice lineární regrese z přibližně posledních tří hodin,
- předpověď se zveřejní až po přibližně 175 minutách platných dat.

Slovní intenzita trendu pro displej používá praktická pásma článku Zbotic:

- méně než -2 hPa / 3 h: rychlý pokles,
- -2 až -0,5 hPa / 3 h: pokles,
- -0,5 až +0,5 hPa / 3 h: stabilní,
- +0,5 až +2 hPa / 3 h: vzestup,
- více než +2 hPa / 3 h: rychlý vzestup.

Klasický výpočet Zambretti používá vlastní hranici trendu +/-0,1 hPa/h:

- `trend >= +0.1`: rising,
- `trend <= -0.1`: falling,
- jinak steady.

## Klasický výpočet A-Z

Po volitelné korekci tlakem podle 16 směrů větru a sezónní korekci se vypočítá index:

```text
rising:  F = 0.1740 * (1031.40 - pressure)
falling: F = 0.1553 * (1029.95 - pressure)
steady:  F = 0.2314 * (1030.81 - pressure)
```

Zaokrouhlený index se omezí na rozsah příslušné tabulky a převede na kód A-Z. Pro severní polokouli se od dubna do září přičte 3,2 hPa u rostoucího trendu a odečte 3,2 hPa u klesajícího trendu.

Směr větru je nepovinný. Je-li ve WU měření dostupný `winddir`, převede se na jednu ze 16 světových stran a použije se klasická korekční tabulka. Bez platného směru větru se algoritmus provede bez této korekce.

## Zdroje algoritmu

- Zbotic, tlakové trendy a doporučené zobrazení:
  https://zbotic.in/barometric-pressure-trend-weather-prediction-algorithm/
- pywws, klasický Zambretti A-Z včetně LUT, větru a sezóny:
  https://pywws.readthedocs.io/en/legacy/_modules/pywws/ZambrettiCore.html
- SAS příklad, teplotně a výškově korigovaný tlak u hladiny moře:
  https://github.com/sassoftware/iot-zambretti-weather-forcasting

## Omezení

Lokální barometrická předpověď je orientační. Nezná radarová a družicová data, fronty, proudění ve výšce ani vývoj srážkových pásem. Pro bezpečnostní rozhodování používejte oficiální meteorologickou předpověď a výstrahy.
