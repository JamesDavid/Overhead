#requires -version 5
# Grab the device screen (full-res JPEG) via the web endpoint.
# Stable single-command invocation so it can be allow-listed once.
param([string]$ip = "192.168.86.92")
$ErrorActionPreference = "Stop"
$jpg = Join-Path $PSScriptRoot "..\.pio\shot.jpg"
& curl.exe -s -m 12 "http://$ip/api/screen.jpg" -o $jpg
Write-Output $jpg
