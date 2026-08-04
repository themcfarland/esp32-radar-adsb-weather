# Řešení problémů

## Serial Monitor nic nevypisuje

- nastavte 115200 baud,
- zavřete a znovu otevřete monitor po resetu,
- ověřte správný USB port,
- zkuste nativní USB CDC i UART0 podle zapojení desky.

## Displej je posunutý nebo cyklicky ujíždí

- proveďte `pio run -t clean`,
- ověřte hlášení o bounce bufferu 20 řádků,
- nemaňte runtime pixel clock,
- po nahrání proveďte úplný power-cycle,
- ověřte, že nejde o variantu 7B/7C.

## Předpověď nefunguje

V Serial Monitoru hledejte:

```text
Forecast Open-Meteo: HTTP 200
Forecast: OK, source=Open-Meteo, cards=6
```

WU `HTTP 401` znamená, že klíč nemá přístup k hodinovému TWC produktu. Open-Meteo má přesto fungovat samostatně.

## Aktuální PWS data nefungují

- zkontrolujte WU API klíč,
- zkontrolujte ID stanice,
- ověřte Wi-Fi a správný čas.

## ADS-B se nezobrazuje

- otevřete `aircraft.json` v prohlížeči ve stejné síti,
- ověřte IP adresu a port,
- zkontrolujte VLAN, firewall a izolaci Wi-Fi klientů,
- odpověď musí obsahovat pole `aircraft`.

## Build hlásí Network.h

Použijte projektový `platformio.ini`, který má:

```ini
lib_ldf_mode = deep
lib_compat_mode = soft
```

Nepřepínejte projekt na starou standardní platformu Arduino-ESP32 2.x.

## Po restartu se obnoví špatný výřez

NVS používá namespace `mapview`. Pro návrat na celou ČR několikrát klepněte do mapy, dokud se nezobrazí `MAPA: CELA CR`; po krátké prodlevě se tento stav uloží.
