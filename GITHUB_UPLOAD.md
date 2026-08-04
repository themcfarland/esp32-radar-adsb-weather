# Publikace projektu na GitHub

Doporučený název repozitáře:

```text
esp32-radar-adsb-weather
```

Níže uvedené příklady používají účet `OK5TVR`. Při jiném názvu účtu nebo repozitáře upravte URL.

## Varianta A – Git z PowerShellu

### 1. Vytvoření repozitáře na GitHubu

Na GitHubu vytvořte nový prázdný repozitář:

```text
OK5TVR/esp32-radar-adsb-weather
```

Při vytváření nezaškrtávejte automatické vytvoření README, `.gitignore` ani licence, protože tyto soubory už projekt obsahuje.

### 2. První nahrání

Otevřete PowerShell v kořenové složce projektu:

```powershell
git init
git branch -M main
git add .
git status
git commit -m "Initial public release v0.16.0"
git remote add origin https://github.com/OK5TVR/esp32-radar-adsb-weather.git
git push -u origin main
```

Před commitem zkontrolujte, že `include/secrets.h` není mezi soubory k odeslání.

### 3. Vytvoření značky a release

```powershell
git tag -a v0.16.0 -m "Touch map zoom and persistent viewport"
git push origin v0.16.0
```

Potom na GitHubu otevřete `Releases`, vytvořte release z tagu `v0.16.0` a jako přílohu můžete přidat původní projektový ZIP.

## Pokud repozitář už existuje

Nejbezpečnější je existující repozitář nejprve naklonovat a nové soubory zkopírovat do jeho pracovní složky. Složku `.git` zachovejte. Potom spusťte:

```powershell
git status
git add -A
git commit -m "Update firmware to v0.16.0"
git push
```

Pokud máte projekt již lokálně a pouze chybí vzdálený repozitář:

```powershell
git remote -v
git remote add origin https://github.com/OK5TVR/esp32-radar-adsb-weather.git
git branch -M main
git push -u origin main
```

Při hlášení `remote origin already exists` použijte místo `remote add`:

```powershell
git remote set-url origin https://github.com/OK5TVR/esp32-radar-adsb-weather.git
```

## Varianta B – nahrání přes web

Webový upload je vhodný jen pro první malý projekt. Na stránce prázdného repozitáře zvolte `uploading an existing file`, přetáhněte obsah kořenové složky a potvrďte commit.

Při tomto způsobu ručně vynechte:

```text
include/secrets.h
.pio/
.vscode/
```

Pro další aktualizace je vhodnější Git z příkazové řádky.

## Další aktualizace

```powershell
git status
git add .
git commit -m "Popis změny"
git push
```

## Publikace GitHub Wiki

Složka `wiki/` v hlavním repozitáři slouží jako zdroj připravených stránek. GitHub Wiki je samostatný Git repozitář.

1. V `Settings → Features` zapněte Wiki.
2. Otevřete kartu `Wiki` a vytvořte první stránku `Home`.
3. V PowerShellu vedle hlavního projektu spusťte:

```powershell
git clone https://github.com/OK5TVR/esp32-radar-adsb-weather.wiki.git
Copy-Item .\esp32-radar-adsb-weather\wiki\*.md .\esp32-radar-adsb-weather.wiki\ -Force
Set-Location .\esp32-radar-adsb-weather.wiki
git add .
git commit -m "Add project documentation"
git push
```

Při práci přímo z kořenové složky projektu lze použít připravený skript:

```powershell
.\scripts\publish_wiki.ps1 -WikiRepositoryUrl "https://github.com/OK5TVR/esp32-radar-adsb-weather.wiki.git"
```

## Doporučený postup před každým push

```powershell
git status
git diff
git diff --cached
```

Ověřte zejména, že v commitu nejsou hesla, API klíče, interní IP adresy, které nechcete zveřejnit, nebo velké soubory z `.pio`.
