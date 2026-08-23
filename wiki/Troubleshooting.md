# Troubleshooting

## Nevidím konfigurační AP

Očekávané SSID:

```text
Radar-ADSB-Setup-XXXX
```

Heslo:

```text
radarsetup
```

Pokud je zařízení připojeno k některému povolenému Wi-Fi profilu, AP se automaticky vypíná. Při ztrátě Wi-Fi firmware postupně zkouší uložené profily; po neúspěšném reconnectu se znovu aktivuje konfigurační AP.

## Uložil jsem špatné heslo nebo profil Wi-Fi

Po restartu zařízení zkusí připojení. Když selže, vrátí se do konfiguračního AP. Připojte se na `192.168.4.1` a údaje opravte.

## `ADSB local: HTTP -11`

`-11` odpovídá timeoutu při čtení. Ověřte URL lokálního `aircraft.json` v browseru a LAN dostupnost přijímače. Internetová adsb.fi vrstva může fungovat i bez lokálního receiveru.

## adsb.fi HTTP 200, ale JSON je neúplný

Firmware používá PSRAM body buffer a až poté JSON parser. Pokud Serial ukazuje timeout při těle HTTP, jde o síťový/TLS přenos, nikoli o neplatnou API URL.

## `setSocketOption(): Bad file number`

Tato hláška se může objevit po uzavření síťového socketu v Arduino-ESP32. Pokud následující request a funkce normálně pokračují, nejde sama o sobě o fatální chybu.

## LightningMaps je připojeno, ale nejsou blesky

Feed může legitimně vracet prázdné `strokes`. Diagnostika rozlišuje živý JSON stream od samotné přítomnosti blesků. Při zastavení platných rámců watchdog spojení obnoví.

## Blesky vypadají jako svislé čáry

Aktuální větev používá plain JSON LightningMaps a blesky kreslí nezávisle na radarových snímcích. Čerstvý zásah je blesk, starší stopa je menší bod/kříž. Pokud se problém vrátí, porovnejte `lat/lon/id` ze Serial logu se zdrojem LightningMaps.

## OTA se nahraje, ale displej během zápisu vypadá špatně

Aktuální OTA před zápisem vypíná podsvícení a do LVGL během flashování nesahá. Používejte firmware z aktuální větve 0.29.x nebo novější.

## BMP180 nenalezen

Zkontrolujte 3V3/GND, SDA GPIO8, SCL GPIO9 a čip ID `0x55`. Nepoužívejte 5 V, pokud modul není pro 5 V jednoznačně určen.


## Lokální ADS-B přijímač není dostupný

Pokud lokální receiver nepoužíváte, vypněte v Nastavení volbu **Používat lokální ADS-B přijímač**. Uložená URL se zachová a provoz pro ČR dál dodává adsb.fi. Od verze 0.30.0 probíhá lokální HTTP požadavek v síťovém workeru; jeho timeout tedy nezastaví mapu ani LVGL. Při opakovaném výpadku se další pokusy automaticky odkládají.

## Síťový zdroj je pomalý nebo nedostupný

Od 0.30.0 se HTTP/TLS zdroje zpracovávají po jednom na pozadí. Poslední dobrá data zůstávají na obrazovce a vadný zdroj přejde do backoffu. Na `/diagnostics` sledujte **Aktivní úlohu**, **Poslední výsledek**, **Nejdelší úlohu**, **Chyby** a **Backoff**. Výpadek jednoho zdroje by neměl zastavit hodiny, radarovou animaci, dotyk ani překreslování mapy.

## Obraz se při zátěži posune

Od verze **0.30.0** jsou dlouhé runtime HTTP/TLS operace přesunuty mimo hlavní smyčku do `NetworkWorker`, takže výpadek adsb.fi, ČHMÚ, Open-Meteo/WU nebo lokálního ADS-B nemá čekáním blokovat LVGL/mapu. Stále zůstává load guard z 0.29.4 a navíc síťový guard po mimořádně dlouhé nebo chybové síťové úloze. Oba mechanismy mohou naplánovat jednorázové srovnání RGB DMA, nejvýše jednou za ochranný interval; nejde o periodický restart panelu. Pokud by obraz zůstal posunutý, ruční **Srovnat LCD** je stále na webu. V diagnostice zkontrolujte také kartu **Síťový worker** a položku nejdelší úlohy.


## Radar hlásí „index nedostupný, cache běží“ a letadla

Tato hláška patří pouze radaru ČHMÚ. Od 0.30.4 nesmí krátký výpadek radarového indexu vyprázdnit ADS-B vrstvu: worker upřednostní local/adsb.fi a poslední platná aircraft cache zůstane dočasně zobrazena. Pokud letadla přesto zmizí, zkontrolujte v Diagnostice `ADSB`, `Síťový worker / Aktivní úloha` a `Poslední úloha`.

## Web nejde otevřít a několik datových zdrojů současně stojí

Od v0.30.5 je LightningMaps WSS mimo hlavní UI loop. Pokud navíc firmware zjistí současný výpadek několika nezávislých zdrojů při stále hlášeném `WL_CONNECTED`, provede Wi-Fi recovery a zpřístupní AP `Radar-ADSB-Setup-XXXX` (`192.168.4.1`, heslo `radarsetup`). Tento mechanismus je záměrně konzervativní a má 10min cooldown.


### Lokalni letadla jsou videt, ale adsb.fi ne

V Diagnostice zkontrolujte blok ADS-B internet. Zobrazuje posledni zdroj, HTTP kod, stari pokusu/uspechu, pocet po sobe jdoucich chyb a cas do dalsiho pokusu. Tlacitko **Obnovit internetove ADS-B** zrusi aktualni backoff a okamzite zaradi novy request bez restartu Wi-Fi. Automaticky backoff je omezen na 60 s a pri vyprseni internetove cache se recovery pokus vynuti nejpozdeji kazdych 30 s.

## Internetové HTTPS zdroje přestanou fungovat, lokální ADS-B běží

Od v0.30.9 sledujte v Diagnostice položku **TLS guard** se stavy `OK`,
`VAROVANI` a `KRITICKA RAM`. Největší interní blok přibližně 30-36 kB už není
důvodem k preventivnímu odpojení LightningMaps: HTTPS se normálně zkusí.
Pokud skutečný TLS/socket request selže při paměťovém tlaku (méně než 48 kB
volné interní RAM nebo největší blok pod 36 kB), firmware jednou uvolní
LightningMaps WSS na 20 s a provede rychlejší další pokus. Tento recovery se
opakuje až po dalším úspěšném spojení, takže WSS není cyklicky odpojován.

Kritický pre-flight stav začíná pod 38 kB volné interní RAM nebo pod 26 kB
největšího bloku. V tomto stavu se jedna HTTPS úloha krátce odloží a WSS se
uvolní jako bezpečnostní opatření. Návrat z kritického stavu používá hysterézi
(44 kB free a 32 kB largest), aby stav nekmital kolem hranice. Skutečné HTTP
chyby 4xx/5xx u adsb.fi WSS recovery nespouštějí.

adsb.lol se po DNS/TLS/socket chybě adsb.fi nespouští okamžitě, aby nevznikl
druhý náročný TLS handshake.
