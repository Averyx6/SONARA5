$ErrorActionPreference = "Stop"

$Root = Join-Path $env:LOCALAPPDATA "SongForgeLocal"
$Ace = Join-Path $Root "ACE-Step-1.5"
New-Item -ItemType Directory -Force -Path $Root | Out-Null

function Get-Uv {
    $cmd = Get-Command uv -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidate = Join-Path $env:USERPROFILE ".local\bin\uv.exe"
    if (Test-Path $candidate) { return $candidate }

    Write-Host "Installing uv..." -ForegroundColor Cyan
    powershell -ExecutionPolicy ByPass -Command "irm https://astral.sh/uv/install.ps1 | iex"
    if (Test-Path $candidate) { return $candidate }

    $cmd = Get-Command uv -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "uv installation was not found after setup."
}

$uv = Get-Uv

if (-not (Test-Path $Ace)) {
    Write-Host "Downloading ACE-Step 1.5..." -ForegroundColor Cyan
    $zip = Join-Path $Root "ace-step.zip"
    Invoke-WebRequest "https://github.com/ACE-Step/ACE-Step-1.5/archive/refs/heads/main.zip" -OutFile $zip
    Expand-Archive -Force $zip $Root
    Remove-Item $zip -Force
}

Set-Location $Ace

Write-Host "Installing/updating local engine dependencies..." -ForegroundColor Cyan
& $uv sync

$env:ACESTEP_API_HOST = "0.0.0.0"
$env:ACESTEP_API_PORT = "8001"
$env:ACESTEP_API_KEY = ""
$env:ACESTEP_INIT_LLM = "auto"

Write-Host ""
Write-Host "Starting SongForge Local engine on port 8001." -ForegroundColor Green
Write-Host "The first run downloads the ACE-Step models (~10 GB core models)." -ForegroundColor Yellow
Write-Host "Keep this window open while generating from your phone." -ForegroundColor Yellow
Write-Host ""
Write-Host "If Windows asks for firewall access, allow Private networks." -ForegroundColor Yellow
Write-Host ""

try {
    $ip = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -notlike "127.*" -and $_.IPAddress -notlike "169.254*" -and $_.InterfaceOperationalStatus -eq "Up" } | Select-Object -First 1 -ExpandProperty IPAddress)
    if ($ip) {
        Write-Host ("Enter this in the Android app: http://" + $ip + ":8001") -ForegroundColor Green
    }
} catch {}

& $uv run acestep-api
