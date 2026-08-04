# Waveshare 7" – radar ČR + ADS-B + počasí

Startovní projekt pro **PlatformIO / Arduino** a původní desku
**Waveshare ESP32-S3-Touch-LCD-7** s řadičem LCD ST7262 a dotykem GT911.

> Projekt není určen pro varianty 7B/7C bez úpravy ovladače displeje.


## Verze 0.9.0 – hodinová předpověď do 48 hodin

Šest předpovědních karet nyní používá hodinový produkt WU/TWC a zobrazuje
časové body `+3 h`, `+6 h`, `+9 h`, `+12 h`, `+24 h` a `+48 h`. U každého
bodu je uvedena teplota, ikona počasí a pravděpodobnost srážek. Automatická
obnova předpovědi probíhá jednou za hodinu.

## Verze 0.7.1 – oprava svislého posunu a SCons buildu

Tato revize navíc odstraňuje pozdní `buildprog` callback, který po úspěšném
vytvoření `firmware.bin` končil chybou `unexpected keyword argument target`.
Ovladač displeje se nyní patchuje přímo při načtení PlatformIO pre-build
skriptu, tedy před kompilací knihovny.

## Verze 0.7.0 – oprava svislého cyklického posunu obrazu

Tato větev vychází z prakticky funkční verze `v9_radar_gui_fix`. Nemění
geometrii GUI ani časování RGB panelu, které na konkrétní desce fungovalo.
Opravuje pouze stav, kdy se horní část obrazu cyklicky zobrazila přibližně ve
dvou třetinách displeje.

Použité změny:

- původní pixel clock 16 MHz a porch/polarity hodnoty zůstávají beze změny,
- bounce buffer RGB ovladače se při sestavení zvětší z 10 na 20 řádků,
- žádná funkce nemění PCLK ani nerestartuje RGB panel za běhu,
- šest radarových PNG se jednorázově převede do kompaktní RGB332 cache v PSRAM,
- během animace se PNG znovu nedekódují,
- rozsáhlé zápisy do PSRAM pravidelně uvolní procesor pro RGB/LVGL úlohu,
- radarová animace běží po 1400 ms místo 850 ms,
- první stažení radarů proběhne před inicializací LCD; další aktualizace stahují
  běžně jen jeden nový pětiminutový snímek.

Před prvním sestavením této verze jednou smažte adresář `.pio`. V logu buildu
musí být uvedeno:

```text
[display-patch] RGB bounce buffer = 20 lines: patched ...
```

nebo při dalším buildu:

```text
[display-patch] RGB bounce buffer = 20 lines: already configured
```

Po nahrání není běžně nutné odpojovat napájení. Pokud je na panelu stále
cyklicky posunutý starý obraz z předchozího firmwaru, proveďte jeden úplný
power-cycle.

## Co projekt zobrazuje

- vlevo mapu celé České republiky,
- animaci posledních šesti radarových snímků ČHMÚ,
- letadla z lokálního přijímače dump1090/readsb/tar1090,
- vpravo aktuální data stanice Weather Underground `IPLZE179`,
- lokálně vypočtený východ a západ Slunce, jeho aktuální výšku nad obzorem,
- východ a západ Měsíce, jeho aktuální výšku, osvětlení a grafickou fázi,
- šest bodů hodinové předpovědi: +3, +6, +9, +12, +24 a +48 hodin,
- dotyková tlačítka `PAUZA/PLAY` a `OBNOVIT`.

## 1. Nastavení přístupů

Upravte soubor `include/secrets.h`:

```cpp
#define WIFI_SSID       "nazev_wifi"
#define WIFI_PASSWORD   "heslo_wifi"
#define WU_API_KEY      "vas_WU_klic"
#define WU_STATION_ID   "IPLZE179"
#define ADSB_AIRCRAFT_URL "http://192.168.1.170:8080/data/aircraft.json"
```

Skutečný API klíč neukládejte do veřejného GitHub repozitáře. Soubor
`include/secrets.h` je proto uveden v `.gitignore`.

## 2. ADS-B zdroj

Firmware používá přímo ověřenou adresu přijímače:

```text
http://192.168.1.170:8080/data/aircraft.json
```

Před nahráním firmwaru ji otevřete na počítači připojeném ke stejné síti.

Správná odpověď je JSON s polem `aircraft`. Pokud je přijímač v oddělené VLAN
nebo hostovské Wi-Fi, musí být povolena komunikace mezi ESP32 a přijímačem.

## 3. Kompilace a nahrání

1. Otevřete celou složku v editoru s PlatformIO.
2. Připojte desku přes USB.
3. Spusťte **PlatformIO: Build**.
4. Spusťte **PlatformIO: Upload**.
5. Sériový monitor používá rychlost `115200 baud`.

Při problému s uploadem podržte BOOT, krátce stiskněte RESET a poté spusťte
nahrávání znovu.

## Datové zdroje a intervaly

| Zdroj | Použití | Obnova |
|---|---|---:|
| ČHMÚ Open Data | šest PNG radarových snímků | 5 min |
| lokální `aircraft.json` | poloha, kurz, výška a volací znak | 2 s |
| Weather Underground PWS | aktuální stanice IPLZE179 | 5 min |
| WU/TWC hourly forecast | body +3, +6, +9, +12, +24 a +48 h | 1 h |
| lokální astronomický výpočet | Slunce, Měsíc a fáze | 1 min |

Radarové soubory se ukládají do LittleFS. Dva mapové buffery a kompaktní
radarová cache jsou v PSRAM. Velký PNG dekódovací buffer se používá jen při
přípravě cache a potom se uvolní.


## Astronomický panel

Panel `Slunce a Mesic` používá souřadnice vrácené stanicí `IPLZE179`. Pokud je
Weather Underground nedostupný, použijí se záložní souřadnice z
`include/config.h`. Přesný čas získává ESP32 přes NTP a časové pásmo je nastaveno
na Českou republiku včetně automatického přechodu mezi CET a CEST.

Zobrazené údaje:

- `S V` – dnešní východ Slunce,
- `S Z` – dnešní západ Slunce,
- aktuální výška Slunce ve stupních; záporná hodnota znamená polohu pod obzorem,
- `M V` a `M Z` – dnešní východ a západ Měsíce,
- aktuální výška Měsíce a procento osvětlené části,
- lokálně vykreslená ikona fáze Měsíce.

Výšky a fáze se obnovují každou minutu. Časy východů a západů se přepočítají
pouze při změně data nebo souřadnic, aby se zbytečně nezatěžoval procesor. Jde o
praktický astronomický výpočet vhodný pro informační displej; u Měsíce se může
čas proti profesionální efemeridě lišit o několik minut.

## Weather Underground/TWC: hodinová předpověď do 48 hodin

Firmware používá hodinový produkt TWC přes endpoint
`/v3/wx/forecast/hourly/2day`. Ze záznamů vybírá první platnou hodinu v čase
+3, +6, +9, +12, +24 a +48 hodin. Každá karta zobrazuje teplotu, ikonu počasí
a pravděpodobnost srážek. Pokud klíč nemá oprávnění k produktu `2day`,
automaticky se zkusí samostatný produkt `3day`. Přesný HTTP stav se vypíše do
Serial Monitoru. Předpověď se automaticky obnovuje jednou za hodinu; tlačítko
`OBNOVIT` ji načte okamžitě.

## Důležité před prvním testem

- Zkontrolujte, že jde skutečně o model bez označení 7B/7C.
- Projekt používá bezpečné nastavení 8 MB flash; na 16MB revizi bude fungovat,
  jen nevyužije celou flash.
- HTTPS certifikáty jsou v této první verzi ověřování zbaveny pomocí
  `setInsecure()`. Pro dlouhodobý provoz je vhodné doplnit kořenové certifikáty.
- Zeměpisná kalibrace používá oficiální rozsah PNG ČHMÚ a projekci EPSG:3857.
  Konstanty jsou v `include/config.h`.
- Zdroj radarových dat musí být na výsledné obrazovce uveden jako ČHMÚ.

## Struktura

```text
include/config.h            rozměry, intervaly a geografické rozsahy
include/models.h            datové struktury
include/secrets.h           Wi-Fi, WU klíč, přesná ADS-B URL
src/adsb_service.*          načtení a parsování aircraft.json
src/weather_service.*       PWS a hodinová předpověď do +48 h
src/astronomy_service.*     Slunce, Měsíc, východy, západy a fáze
src/radar_service.*         index, stažení PNG, PNGdec a projekce
src/map_renderer.*          mapa ČR, města a symboly letadel
src/ui.*                    LVGL obrazovka a dotyková tlačítka
src/main.cpp                plánování aktualizací
```

## Další vhodné kroky

- barevná legenda výšky letadel,
- kliknutí na letadlo a detail letu,
- přiblížení mapy gestem,
- OTA aktualizace přes webové rozhraní,
- ukládání posledních dat pro start bez internetu,
- přepnutí mezi radarovou animací a detailní lokální mapou.

## Plynulá radarová animace bez blikání

Mapa používá dva plnohodnotné RGB565 buffery v PSRAM. Zatímco je jeden buffer
zobrazený, do druhého se skrytě vykreslí základ mapy, radar, hranice, města,
letadla a čas snímku. Až po dokončení celého obrazu se oba LVGL canvasy
atomicky prohodí. Uživatel proto nikdy nevidí mezikrok s vymazanou mapou ani
postupné dekódování PNG.

Dva mapové buffery 600 x 444 px spotřebují přibližně 1,02 MiB PSRAM. Šest
kompaktních radarových vrstev RGB332 spotřebuje přibližně 1,52 MiB. Dočasný
PNG dekódovací buffer pro obraz 1058 x 716 potřebuje přibližně 1,45 MiB, ale po
přípravě animace se uvolní. Funkce `UI::presentMap()` je volána pod
`lvgl_port_lock()`, což je v `main.cpp` dodrženo.


## Důležitá kompatibilita PlatformIO – verze 0.5.2-hw7-network-fix

Knihovna `ESP32_Display_Panel 0.1.4` nepodporuje Arduino-ESP32 2.x. Projekt proto
nepoužívá standardní `espressif32@6.8.1`, ale pevně připnutou platformu
`pioarduino 51.03.07`, která obsahuje Arduino-ESP32 3.0.7 a ESP-IDF 5.1.4.
Tím jsou dostupné RGB framebuffer, bounce buffer, QSPI a LEDC struktury, které
ovladač displeje vyžaduje.

Před prvním sestavením této verze:

1. Zavřete případný Serial Monitor.
2. V PlatformIO zvolte **Clean** nebo smažte složku `.pio` v projektu.
3. Spusťte Build znovu. První sestavení stáhne novou platformu a může trvat déle.
4. Nejprve lze sestavit prostředí `waveshare-display-test`.

Není potřeba ručně měnit globální instalaci PlatformIO ani odinstalovávat běžnou
platformu `espressif32`; tento projekt používá vlastní připnutou platformu pouze
pro sebe.

## Opravy pro skutečný hardware – verze 0.5.2-hw7-network-fix

Tato varianta je pevně určena pro původní **ESP32-S3-Touch-LCD-7**, nikoliv
pro 7B nebo 7C. PlatformIO používá profil `esp32-s3-devkitc-1`, 8MB flash a
8MB OPI PSRAM (`qio_opi`). Ovladač displeje je připnut na vydání
`Waveshare_ST7262_LVGL 0.1.0` spolu s přesnými kompatibilními verzemi LVGL,
ESP32 Display Panel a IO Expander.

Při startu se na sériový port vypíše:

- skutečná velikost flash a PSRAM,
- množství volné PSRAM,
- rozlišení registrované v LVGL,
- důvod posledního resetu.

Firmware se zastaví s čitelnou chybou, pokud není dostupná přibližně 8MB PSRAM
nebo pokud ovladač nezaregistruje displej 800 x 480. Po prvním nahrání je podle
doporučení Waveshare vhodné desku vypnout a znovu zapnout.

### Samostatný test displeje a dotyku

Před nahráním celé aplikace lze v PlatformIO přepnout prostředí na
`waveshare-display-test`. Tento test nevyžaduje Wi-Fi ani API klíč. Zobrazí
zjištěnou flash, PSRAM a rozlišení a obsahuje velké dotykové tlačítko pro
ověření GT911. Potom se vraťte k prostředí
`waveshare-esp32-s3-touch-lcd-7` a nahrajte hlavní aplikaci.

## Oprava chyby `Network.h` (verze 0.5.2)

Pro Arduino-ESP32 3.x nepoužívejte režimy LDF s příponou `+`. Režimy
`deep+` a `chain+` mohou při kompilaci knihovny WiFi vynechat frameworkovou
knihovnu `Network`, což vede k chybě `fatal error: Network.h: No such file or directory`.
Projekt proto používá:

```ini
lib_ldf_mode = deep
lib_compat_mode = soft
```

Před prvním sestavením této verze smažte adresář `.pio` nebo spusťte
`PlatformIO: Clean`, aby se znovu vytvořil graf závislostí.

## Version 0.5.3: linker and DRAM fix

- The main environment now excludes `src/hardware_test.cpp`, preventing duplicate `setup()` and `loop()` definitions.
- The display-test environment still compiles only `hardware_test.cpp`.
- The 256 kB static LVGL heap was removed from internal DRAM and replaced by a PSRAM-backed custom allocator.
- Forecast and Moon canvas pixel buffers are dynamically allocated in PSRAM.
- These changes address `dram0_0_seg overflowed` while preserving the double-buffered map.


## Verze 0.6.0 – oprava radaru ČHMÚ a nové GUI

Tato verze opravuje stahování a vykreslování aktuálních radarových PNG:

- parser přijímá skutečný 39znakový název
  `pacz2gmaps3.z_max3d.YYYYMMDD.hhmm.0.png`,
- nejprve načte adresář ČHMÚ a vybere šest nejnovějších souborů; při chybě
  použije záložní hledání podle UTC času,
- používá produkt `png_masked`, který se vizuálně shoduje s radarovou webovou
  aplikací ČHMÚ,
- rozměr PNG se zjišťuje z hlavičky při běhu; není již chybně předpokládán
  pevný obraz 512 x 512,
- dekódovací buffer se alokuje v PSRAM podle skutečného rozměru,
- obraz se promítá v projekci Web Mercator EPSG:3857 do mapy,
- staré funkční radarové snímky zůstávají v cache, pokud nové stažení selže,
- na mapě se zobrazí konkrétní chybová zpráva místo prázdné vrstvy.

GUI má nově společnou horní lištu přes celých 800 px, větší kartu aktuálního
počasí, kompaktnější astronomii a předpověď v mřížce 2 x 3. Mapa obsahuje
legendu dBZ, čas snímku v UTC, pořadí snímku animace a diagnostický rozměr
zdrojového PNG.

Po nahrání sledujte Serial Monitor. Úspěšný radar vypíše například:

```text
Radar: reading CHMI directory index...
Radar downloaded: pacz2gmaps3.z_max3d.20260804.1025.0.png
...
```

Na mapě se v pravém horním rohu zobrazí například:

```text
Radar 10:25 UTC  6/6  1058x716
```

## Verze 0.12.0 - forecast diagnostics

- Vychází z funkční verze v15 a neobsahuje runtime restart RGB panelu.
- Hodinová předpověď: +3, +6, +9, +12, +24 a +48 hodin.
- Primárně WU/TWC hourly 2day, následně 3day.
- Automatická záloha Open-Meteo funguje i bez WU forecast oprávnění.
- Open-Meteo JSON se parsuje bez filtru, aby se spolehlivě zachovala všechna pole primitivních polí.
- Předpověď se spouští i tehdy, když není nastavený WU klíč.
- Diagnostika se zapisuje současně na USB CDC `Serial` i hardwarový UART `Serial0`.
- Každých 10 sekund se vypisuje HEARTBEAT, takže Serial Monitor lze otevřít i po startu.


## v19 – Open-Meteo HTTP/JSON fix

Open-Meteo can return an HTTP/1.1 chunked response. Passing `HTTPClient::getStream()`
directly to ArduinoJson then exposes the chunk-size markers and produces
`DeserializationError::InvalidInput`. Version v19 requests HTTP/1.0 with identity
encoding and parses the decoded body returned by `HTTPClient::getString()`.
Open-Meteo is now the primary forecast source; WU/TWC hourly products are used
only as a fallback because the PWS key returned HTTP 401 for them.


## Update v0.15.0
- Czech Republic outline replaced with 300 original vertices selected from the user-provided geoBoundaries CZE ADM0 GeoJSON.
- Boundary source: geoBoundaries, CC BY 4.0.
- Radar, ADS-B, Weather Underground current conditions and Open-Meteo forecast are unchanged from the verified v19 base.


## Update v0.16.0 – touch map zoom
- tap the map to cycle: full Czech Republic -> 50 km -> 25 km -> 10 km -> full Czech Republic,
- every zoom is centered on the tapped position,
- radar, border, cities, station and ADS-B aircraft use the same viewport,
- the final zoom mode and map center are saved to ESP32 NVS after a short delay and restored after restart,
- the radar zoom reuses the compact animation cache to preserve RGB display stability.


## Verze 0.17.0 – stabilní aktualizace radaru

- Po spuštění RGB displeje se nové radarové PNG již nezapisují do LittleFS.
- Aktualizace stáhne pouze nejnovější snímek do PSRAM, dekóduje jej po řádcích přímo do kompaktní RGB332 vrstvy a posune animační cache.
- Startovní stažení a trvalá cache na LittleFS zůstávají zachovány před inicializací LCD.
- Mazání neexistujících souborů je předem kontrolováno, takže nevznikají opakované chyby `remove(): ... does not exist`.
- Po radarové aktualizaci a po zápisu nastavení mapy do NVS se jednorázově vyžádá synchronizace RGB DMA na dalším VSYNC.
- Runtime fallback již nestahuje šest PNG do flash; při nedostupném indexu zůstává zobrazena poslední funkční RAM cache.
