# Waveshare 7" Radar ČR + ADS-B + počasí


## 0.28.5 - Blitzortung realtime + 10 km varovny kruh + stabilni OTA obrazovka

- Blitzortung se prijima realtime pres WSS (`ws7/ws1/ws8.blitzortung.org`) a LZW zpravy se dekoduji primo na ESP32.
- Jednotlive blesky `time/lat/lon` se ukladaji do kruhoveho PSRAM bufferu a zobrazuji ve stejnych 5min casovych slotech jako CHMI radar.
- WebSocket prijima blesky prubezne; po kazde petiminutove aktualizaci radaru se pouze posunou hranice sest casovych slotu.
- Radar a blesky pouzivaji stejny index animace a stejne tlacitko pauzy.
- Webove nastaveni obsahuje OTA upload `firmware.bin`; zapisuje se do neaktivni OTA partition a po uspechu se ESP32 restartuje. NVS nastaveni zustava zachovano.

Firmware pro původní desku **Waveshare ESP32-S3-Touch-LCD-7**
(800 × 480, ST7262, GT911, CH422G, 8 MB OPI PSRAM).

Aktuální verze: **0.28.5-ota-screen**

> Projekt není určen pro varianty 7B/7C bez úpravy ovladače displeje.

## Blesky Blitzortung ve verzi 0.28.5

Mapa obsahuje volitelnou vrstvu **Blesky Blitzortung**. Firmware se pripojuje pres zabezpeceny WebSocket na `ws7.blitzortung.org` a pri vypadku zkusi `ws1` a `ws8`. Po spojeni odesle odber `{"a":111}`. Prichozi textove ramce jsou LZW komprimovane; firmware dekoduje pouze zacatek zpravy s poli `time`, `lat` a `lon`, pole `sig` se kvuli pameti neparsuje.

Blesky z okoli CR se ukladaji do kruhoveho PSRAM bufferu. Pri vykresleni se kazdy blesk zaradi do intervalu `(cas_radaru - 5 min, cas_radaru]`, takze tlacitko pauzy i sestikrokova animace zustavaji spolecne s CHMI radarem. Po startu se historie plni postupne, protoze WebSocket dodava realtime data a neposila zpetne stare blesky.

Navic bezi nezavisle realtime varovani pro domaci pozici `49.7863 N, 13.2850 E`. Pokud byl v poslednich 10 minutach prijat alespon jeden blesk do vzdalenosti 10 km, kolem domaci znacky se vykresli cerveny geograficky kruh s realnym polomerem 10 km. Vzdalenost blesku se pocita po povrchu Zeme (Haversine), takze podminka neni zavisla na zoomu ani na pixelove velikosti mapy. Varovny kruh je nezavisly na prave zobrazenem historickem radarovem snimku a sam zmizi po vyprseni desetiminutoveho okna.

OTA aktualizace z webu zustava zachovana.

## OTA aktualizace

Na hlavní webové stránce je karta **OTA aktualizace firmware**. Vyberte
PlatformIO `firmware.bin` a odešlete jej přes formulář. Firmware se zapisuje do
neaktivní OTA partition `app0/app1`; po úspěšném dokončení se zařízení samo
restartuje. NVS konfigurace se nemaže.

## Kalibrace tlaku ve verzi 0.28.0

Webové nastavení nyní umí z aktuálního tlaku BMP180 a referenčního tlaku z WU vypočítat navrženou nadmořskou výšku senzoru. Hodnota se před uložením zobrazí ve formuláři a jemná korekce MSL se nastaví na nulu.

## Oprava BMP180 ve verzi 0.27.0

Původní detekce používala Arduino objekt `Wire`, ale ovladač displeje inicializuje
sdílenou sběrnici GPIO8/GPIO9 přímo přes ESP-IDF. `Wire` proto nebyl aktivní a
BMP180 na adrese `0x77` nebyl nalezen.

Nová verze:

- používá již inicializovaný řadič **I2C0**,
- nevolá `Wire.begin()` a nemění konfiguraci dotyku ani CH422G,
- ověřuje identifikátor BMP180 `0x55`,
- načte a zkontroluje tovární kalibrační konstanty,
- provede zkušební měření tlaku a teploty,
- při startu zkouší senzor čtyřikrát,
- při měření krátce uzamkne LVGL, aby současně neprobíhalo čtení GT911.

V sériovém monitoru se při správném připojení zobrazí:

```text
Barometer: BMP180 detected on shared ESP-IDF I2C0 at 0x77, chip ID 0x55
```

## Novinky ve verzi 0.26.0

### Úvodní obrazovka systému

Po inicializaci LCD se zobrazí samostatná startovací obrazovka:

- animovaný kruhový indikátor,
- jednořádkový popis právě prováděné operace,
- procentní průběh inicializace,
- verze firmware,
- podpis `Vytvoril OK5TVR`.

Během startu se vypisuje kontrola LCD, stav Wi-Fi nebo konfiguračního AP,
příprava mapových bufferů, detekce I2C barometru, tvorba radarové cache,
načítání počasí, Zambretti výpočtu, astronomických údajů a ADS-B. Hlavní
obrazovka se vytváří skrytě a zobrazí se až po dokončení inicializace.
Podsvícení zůstává během úvodní obrazovky zapnuté; týdenní plán se aplikuje
až po přechodu do hlavního rozhraní.

## Novinky ve verzi 0.25.0

- původní jednoduchý textový odhad nahrazen algoritmem **Zambretti**,
- výstup jednoho z 26 stavů označených kódem `A` až `Z`,
- výpočet z tlaku přepočteného na hladinu moře, tříhodinového trendu a ročního období,
- volitelná korekce podle směru větru z aktuálních dat Weather Underground,
- přepočet tlaku na hladinu moře používá 12hodinový průměr venkovní teploty z WU,
- tlakový trend se počítá z přímo naměřeného tlaku senzoru, takže změna teploty nevytváří falešný trend,
- při nedostupné nebo starší než 12hodinové WU teplotě se použije standardních 15 °C,
- pětibodové slovní hodnocení trendu podle změny tlaku za tři hodiny,
- Zambretti kód, trend, sezónní korekce a použití větru ve webové diagnostice,
- pravý panel zobrazuje tlak, změnu za tři hodiny, Zambretti kód a českou předpověď.

## Hlavní funkce

- animace radarových snímků ČHMÚ,
- lokální ADS-B data z `aircraft.json`,
- aktuální počasí z Weather Underground PWS,
- předpověď Open-Meteo pro `+3`, `+6` a `+9 h`,
- lokální Zambretti předpověď s 24hodinovým grafem tlaku a projekcí trendu,
- Slunce, Měsíc a astronomické údaje,
- dotykový zoom mapy: celá ČR, 50 km, 25 km a 10 km,
- uložení posledního výřezu mapy do NVS,
- runtime radarové aktualizace v PSRAM bez zápisu PNG do LittleFS,
- první nastavení přes Wi-Fi AP,
- trvale dostupné webové nastavení v domácí Wi-Fi,
- grafické zvýraznění až tří letounů podle callsignu nebo ICAO hex,
- nezávislé zapínání radarové, bleskové Blitzortung a ADS-B vrstvy,
- místní hodiny a datum s automatickým CET/CEST,
- webová diagnostika na `/diagnostics`,
- týdenní plán podsvícení od pondělí do neděle,
- dočasné probuzení zhasnutého displeje dotykem na jednu minutu.

## Připojení barometru

Podporovaný senzor v této hardwarově ověřované větvi:

- **BMP180** na pevné I²C adrese `0x77`.

Zapojení do externího I²C konektoru desky:

```text
Barometr       Waveshare ESP32-S3-Touch-LCD-7
------------------------------------------------
VCC / VIN      3V3
GND            GND
SDA            GPIO8
SCL            GPIO9
```

Použijte napájení **3,3 V**. Sběrnice je sdílená s dotykovým panelem GT911
a IO expandérem CH422G. Firmware proto nevytváří novou sběrnici a přistupuje
k BMP180 přes již spuštěný ESP-IDF řadič I2C0. Adresa BMP180 je pevně `0x77`
a očekávaný identifikátor čipu je `0x55`.

Označení modulu „MB120“ není typ čipu. Pro tuto verzi byl potvrzen skutečný
čip **BMP180**, kterému je přizpůsoben ovladač.

## Nastavení barometru ve webu

Na hlavní stránce nastavení je sekce **I2C barometr a Zambretti**:

- **Povolit barometr** – zapne automatickou detekci senzoru,
- **Nadmořská výška senzoru** – používá se pro přepočet na tlak u hladiny moře,
- **Korekce tlaku** – jemná kalibrace v hPa.

Pro smysluplné porovnání s meteorologickými údaji zadejte skutečnou nadmořskou
výšku místa, kde je senzor umístěný. Korekci ponechte nejprve na `0.0 hPa` a
upravte ji až po porovnání s referenční stanicí.

Nastavení se ukládá do NVS. Změna nadmořské výšky nebo korekce vymaže starou
historii, protože staré body byly vypočtené jiným převodem.

## Grafické zpracování pravého panelu

Horní část pravého panelu zůstává pro aktuální počasí a astronomii.

Pod ní jsou tři kompaktní karty internetové předpovědi:

```text
+3 h       +6 h       +9 h
ikona      ikona      ikona
teplota    teplota    teplota
srážky     srážky     srážky
```

Zbylý prostor využívá graf tlaku:

- **modrá plná čára** – skutečný průběh za posledních 24 hodin,
- **bílý bod a svislá čára** – aktuální okamžik,
- **žlutá přerušovaná čára** – jednoduchá projekce trendu do `+3`, `+6` a `+9 h`,
- automaticky přizpůsobený rozsah osy tlaku.

Pod grafem se zobrazuje:

- aktuální tlak přepočtený na hladinu moře,
- změna odvozená pro tři hodiny,
- slovní trend,
- orientační lokální odhad počasí.

## Jak vzniká Zambretti předpověď

Senzor se čte jednou za minutu. Do historie se ukládá jeden bod přibližně po
pěti minutách, celkem maximálně 289 bodů, tedy přibližně 24 hodin včetně
aktuálního bodu. Historie zůstává pouze v RAM.

Naměřený tlak se nejprve přepočítá na tlak u hladiny moře. Převod používá:

- tlak ze senzoru,
- 12hodinový průměr venkovní teploty z Weather Underground,
- nadmořskou výšku z webového nastavení,
- volitelnou kalibrační korekci v hPa.

Trend se počítá lineární regresí z bodů za poslední tři hodiny. Změna za tři
hodiny se současně slovně rozdělí na rychlý pokles, pokles, stabilní stav,
vzestup nebo rychlý vzestup.

Samotný algoritmus Zambretti používá:

- tlak přepočtený na hladinu moře,
- trend v hPa za hodinu,
- měsíc a severní polokouli,
- volitelný směr větru.

Pokud je dostupné aktuální měření Weather Underground, převezme se z něj směr
větru a použije se šestnáctibodová větrná korekce. Bez WU dat algoritmus pracuje
bez větrné korekce. Sezónní korekce se použije pouze po synchronizaci NTP, kdy
je známý místní měsíc.

Výsledkem je kód `A` až `Z` a krátký český text, například:

```text
Z:C  Vyjasnovani
Z:N  Prehanky, jasne intervaly
Z:Z  Bourlivo, vydatny dest
```

Zambretti je kategoriální lokální předpověď přibližně pro několik dalších
hodin, nikoliv samostatná numerická předpověď pro přesné časy `+3`, `+6` a
`+9 h`. Tyto tři karty nad grafem nadále pocházejí z Open-Meteo nebo WU.
Žlutá přerušovaná čára v grafu je pouze lineární projekce tlakového trendu.

Po startu musí být nejprve získána téměř celá tříhodinová historie (přibližně
175 minut při pětiminutových bodech). Do té doby se zobrazuje `Z:-` a informace
o sběru trendu. Po restartu nebo odpojení napájení začíná historie znovu.

Implementace vychází z principů popsaných v článku Zbotic a z klasické
referenční implementace pywws. Článek Zbotic používáme pro pět slovních pásem
změny tlaku; vlastní kód `A` až `Z`, prahovou hodnotu trendu, sezónní korekci,
větrnou korekci a vyhledávací tabulky přebíráme z klasického Zambretti postupu:

- https://zbotic.in/barometric-pressure-trend-weather-prediction-algorithm/
- https://pywws.readthedocs.io/en/legacy/_modules/pywws/ZambrettiCore.html

Tlaková předpověď je orientační. Nezahrnuje synoptické mapy, vlhkost ve výšce,
frontální rozhraní ani vývoj srážkových pásem a nenahrazuje oficiální
meteorologickou předpověď.

## První spuštění

Po prvním nahrání firmware zařízení vytvoří konfigurační přístupový bod:

```text
SSID: Radar-ADSB-Setup-XXXX
Heslo: radarsetup
Adresa: http://192.168.4.1/
```

1. Připojte telefon nebo počítač k síti `Radar-ADSB-Setup-XXXX`.
2. Otevřete `http://192.168.4.1/`.
3. Vyplňte domácí Wi-Fi, ADS-B URL, případně WU a barometr.
4. Stiskněte **Uložit nastavení**.
5. Zařízení se připojí k domácí síti.

Při chybě hesla nebo nedostupné síti se konfigurační AP spustí znovu.

## Webové nastavení v domácí síti

Webový server zůstává aktivní i po připojení k domácí Wi-Fi:

```text
http://192.168.1.123/
```

Při funkčním mDNS:

```text
http://radar-adsb.local/
```

Na první stránce je přímý odkaz na diagnostiku:

```text
http://IP_ZARIZENI/diagnostics
```

Web nemá přihlášení. Používejte jej pouze v důvěryhodné lokální síti a
nevystavujte port 80 do internetu.

## Vrstvy mapy a zvýraznění letounů

Ve webu lze nezávisle zapnout:

- radarovou vrstvu,
- letouny ADS-B.

Dostupné kombinace jsou radar + ADS-B, jen radar, jen ADS-B nebo obě vrstvy
vypnuté.

Lze uložit tři callsigny nebo ICAO hex identifikátory. Každý sledovaný letoun
má na mapě vlastní barvu zvýrazňovacího kruhu. Upozornění je pouze grafické.

## Týdenní plán podsvícení

Pro každý den lze nastavit samostatný čas zapnutí a vypnutí. Interval může
přecházet přes půlnoc, například `18:00-02:00`.

Mimo aktivní interval se vypne pouze podsvícení. ESP32, síť a sběr dat dále
pracují. První dotyk zapne podsvícení na 60 sekund a je zachycen překryvnou
vrstvou, takže současně nezmění zoom mapy.

Plán používá místní čas s pravidlem:

```text
CET-1CEST,M3.5.0/2,M10.5.0/3
```

Dokud není NTP synchronizované, podsvícení zůstává zapnuté.

## Teplota pro redukci tlaku

Barometr je obvykle umístěný v místnosti a jeho vlastní teplota může být
ovlivněná elektronikou displeje. Pro meteorologický přepočet tlaku proto
firmware používá venkovní teplotu z Weather Underground.

- jednotlivá WU měření se ukládají pouze v RAM,
- stejné měření se podle pole `epoch` nezapočítá opakovaně,
- používá se klouzavý průměr za posledních nejvýše 12 hodin,
- při změně ID WU stanice se teplotní historie vymaže,
- při prvním přechodu z náhradních 15 °C na WU se znovu založí tlakový graf, aby nemíchal dva různé přepočty,
- chybí-li WU data nebo jsou starší než 12 hodin, použije se 15 °C,
- teplota BMP/BME senzoru se zobrazuje jen jako diagnostický údaj.

Tříhodinový trend se počítá z tlaku přímo naměřeného senzorem před redukcí.
Změna venkovní teploty proto nevytvoří falešný vzestup nebo pokles. Graf a
Zambretti používají tlak přepočtený na hladinu moře.

## Diagnostika

Stránka `/diagnostics` zobrazuje mimo jiné:

- stav Wi-Fi, AP, IP a RSSI,
- heap a PSRAM,
- čas, datum, NTP a CET/CEST,
- radar, ADS-B, počasí a astronomii,
- stav barometru a I²C adresu,
- aktuální tlak a teplotu senzoru,
- počet bodů tlakové historie,
- trend v hPa/h a změnu za tři hodiny,
- Zambretti kód a českou předpověď,
- informaci o korekci směrem větru a ročním obdobím,
- počet překreslení mapy a obnov RGB DMA,
- plán a stav podsvícení.

JSON je dostupný na:

```text
http://IP_ZARIZENI/api/diagnostics
```

Heartbeat nově vypadá například takto:

```text
HEARTBEAT ms=120000 WiFi=1 AP=0 heap=137 kB forecast=Open-Meteo cards=3 layers=R1/A1 alerts=on [CSA123|4B1812|RYR45] BL=1 schedule=1 wake=0 baro=BMP180 1015.8 hPa d3h=-1.2 Z=R
```

## Stabilita RGB displeje

Firmware používá konzervativní 20řádkový bounce buffer. Periodický restart RGB
DMA je vypnutý, protože na testovaném panelu zhoršoval vodorovné posuny.
Tlačítko **Srovnat LCD** ve webu zůstává dostupné pro ruční obnovu.

Radarové PNG se po startu aktualizují přímo v PSRAM bez runtime zápisu do
LittleFS. Tlaková historie je také pouze v RAM.

## Kompilace

1. Otevřete celou složku projektu v PlatformIO.
2. Proveďte **PlatformIO: Clean**.
3. Spusťte **PlatformIO: Build**.
4. Spusťte **PlatformIO: Upload**.
5. Serial Monitor nastavte na `115200 baud`.

Při přechodu ze starší verze je vhodné jednou odstranit `.pio`, aby PlatformIO
odstranilo dříve stažené, nyní již nepoužívané Adafruit knihovny barometru:

```powershell
Remove-Item -Recurse -Force .pio
pio run
```

Ovladač BMP180 je nyní součástí projektu v souborech
`src/bmp180_shared_i2c.*` a nepřidává další knihovní závislost.

## Uložení konfigurace

Do NVS se ukládá:

- Wi-Fi a datové zdroje,
- vrstvy mapy,
- tři sledované letouny,
- týdenní plán podsvícení,
- zapnutí barometru,
- nadmořská výška senzoru,
- korekce tlaku,
- poslední výřez mapy.

Tlaková historie se do NVS ani do LittleFS neukládá.

## Struktura projektu

```text
include/config.h             rozměry, intervaly, I²C piny a AP parametry
include/models.h             datové modely, tlaková historie a diagnostika
src/bmp180_shared_i2c.*      BMP180 na sdílené ESP-IDF sběrnici I2C0
src/barometer_service.*      měření, historie, redukce tlaku a trend
src/zambretti_forecaster.*   čistý výpočet Zambretti A-Z
src/device_config.*          NVS, AP/STA web, barometr a diagnostika
src/weather_service.*        WU a Open-Meteo +3/+6/+9 h
src/adsb_service.*           načtení aircraft.json
src/radar_service.*          ČHMÚ radar a runtime aktualizace v PSRAM
src/map_renderer.*           mapa, vrstvy a zvýrazněné symboly
src/ui.*                     LVGL obrazovka, tlakový graf a dotyk
src/main.cpp                 inicializace a plánování úloh
docs/ZAMBRETTI.md            vzorce, prahy, zdroje a omezení algoritmu
```

## Kalibrace nadmořské výšky barometru

Webové nastavení obsahuje pomocnou kalibraci podle aktuálního tlaku u hladiny moře. Firmware načte přímo měřený tlak BMP180, teplotu používanou pro redukci a volitelně také aktuální tlak z Weather Underground. Z těchto hodnot dopočítá navrženou nadmořskou výšku senzoru.

Příklad: místní tlak `974 hPa` a referenční tlak `1014 hPa` odpovídají přibližně `341 m` při 15 °C nebo `353 m` při 25 °C. Přesný výsledek se proto počítá z aktuálního klouzavého průměru venkovní teploty WU.

Po výpočtu se hodnota pouze vloží do formuláře. Uživatel ji musí potvrdit tlačítkem **Uložit nastavení**. Jemná korekce MSL se při automatickém výpočtu nastaví na `0,0 hPa`; používat se má jen k doladění o malé zbytkové odchylky. Tříhodinový trend se stále počítá z neupraveného tlaku senzoru.



## OTA display

During browser OTA the LCD switches to a minimal static update screen. The main dashboard and network data tasks remain paused while flash is written; the RGB panel is periodically resynchronised to prevent display scrambling.
