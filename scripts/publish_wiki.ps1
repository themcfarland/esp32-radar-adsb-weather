param(
    [Parameter(Mandatory = $true)]
    [string]$WikiRepositoryUrl
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$wikiSource = Join-Path $projectRoot "wiki"
$target = Join-Path (Split-Path -Parent $projectRoot) "esp32-radar-adsb-weather.wiki"

if (-not (Test-Path $wikiSource)) {
    throw "Slozka wiki nebyla nalezena: $wikiSource"
}

if (-not (Test-Path $target)) {
    git clone $WikiRepositoryUrl $target
}

Copy-Item (Join-Path $wikiSource "*.md") $target -Force
Push-Location $target
try {
    git add .
    if (-not (git diff --cached --quiet)) {
        git commit -m "Update project documentation"
        git push
    } else {
        Write-Host "Wiki neobsahuje zadne nove zmeny."
    }
}
finally {
    Pop-Location
}
