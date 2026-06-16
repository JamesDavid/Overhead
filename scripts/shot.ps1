#requires -version 5
# Grab the device screen via the web endpoint and save an upscaled PNG.
# Stable single-command invocation so it can be allow-listed once.
param([string]$ip = "192.168.86.92", [int]$scale = 2)
$ErrorActionPreference = "Stop"
$bmp = Join-Path $PSScriptRoot "..\.pio\shot.bmp"
$png = Join-Path $PSScriptRoot "..\.pio\shot.png"
& curl.exe -s -m 10 "http://$ip/api/screen.bmp" -o $bmp
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile($bmp)
$w = [int]($img.Width * $scale); $h = [int]($img.Height * $scale)
$big = New-Object System.Drawing.Bitmap $img, $w, $h
$big.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
$img.Dispose(); $big.Dispose()
Write-Output $png
