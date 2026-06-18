#requires -version 5
# Grab the device screen (full-res JPEG) via the web endpoint.
# Stable single-command invocation so it can be allow-listed once.
param([string]$ip = "192.168.86.92", [string]$user = "admin", [string]$pass = "overhead")
$ErrorActionPreference = "Stop"
$jpg = Join-Path $PSScriptRoot "..\.pio\shot.jpg"
& curl.exe -s -m 12 -u "${user}:${pass}" "http://$ip/api/screen.jpg" -o $jpg
Write-Output $jpg
