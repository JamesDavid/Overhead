#requires -version 5
# Rebuild every board env and refresh the web-flasher bins under docs/flash/.
# Each variant lives in its own subdir matching its manifest:
#   cyd2usb/        (manifest.json)                - ESP32, bootloader+partitions+firmware
#   elegoo/         (manifest-elegoo.json)          - ESP32, bootloader+partitions+firmware
#   crowpanel/      (manifest-crowpanel.json)       - ESP32-S3, single merged factory image @0
#   crowpanel-adv7/ (manifest-crowpanel-adv7.json)  - ESP32-S3, merged factory image @0 (experimental)
#   crowpanel-hmi5/ (manifest-crowpanel-hmi5.json)  - ESP32-S3, merged factory image @0 (experimental)
# boot_app0.bin is shared at the root by both ESP32 manifests and is static (it comes
# from the framework, never changes), so it is intentionally NOT touched here.
#
# Usage (from anywhere):  pwsh -File scripts/refresh-flasher.ps1
# Then review + commit:   git add -f docs/flash; git commit
param(
  [string]$pio = "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe",
  [string]$crowCore = "$env:USERPROFILE\.platformio-crowpanel",  # CrowPanel's isolated core dir
  # CrowPanel builds MUST use the isolated core dir's OWN launcher (Python 3.11) —
  # the registry launcher auto-updated to 3.12 and dies on a littlefs native-ext
  # mismatch against the 3.11 packages (see memory: platformio-toolchain).
  [string]$pioCrow = "$env:USERPROFILE\.platformio-crowpanel\penv\Scripts\platformio.exe"
)
$ErrorActionPreference = "Stop"
$env:PYTHONIOENCODING = "utf-8"
$root  = Resolve-Path (Join-Path $PSScriptRoot "..")
$build = Join-Path $root ".pio\build"
$flash = Join-Path $root "docs\flash"

function Build-Env([string]$e, [string]$launcher = $pio) {
  Write-Host "==> building $e"
  & $launcher run -e $e
  if ($LASTEXITCODE -ne 0) { throw "build failed: $e" }
}
function Copy-Bin([string]$e, [string]$src, [string]$dstDir, [string]$dstName) {
  $d = Join-Path $flash $dstDir
  if (-not (Test-Path $d)) { New-Item -ItemType Directory -Force $d | Out-Null }
  Copy-Item (Join-Path $build "$e\$src") (Join-Path $d $dstName) -Force
}

# --- ESP32 CYD variants (default registry core dir) ---
Build-Env "cyd28_ili9341"
Copy-Bin "cyd28_ili9341" "bootloader.bin" "cyd2usb" "bootloader.bin"
Copy-Bin "cyd28_ili9341" "partitions.bin" "cyd2usb" "partitions.bin"
Copy-Bin "cyd28_ili9341" "firmware.bin"   "cyd2usb" "firmware.bin"

Build-Env "cyd28_elegoo"
Copy-Bin "cyd28_elegoo" "bootloader.bin" "elegoo" "bootloader.bin"
Copy-Bin "cyd28_elegoo" "partitions.bin" "elegoo" "partitions.bin"
Copy-Bin "cyd28_elegoo" "firmware.bin"   "elegoo" "firmware.bin"

# --- CrowPanel S3 variants (isolated core dir + ITS launcher; merged factory image @0) ---
$env:PLATFORMIO_CORE_DIR = $crowCore
try {
  Build-Env "crowpanel_s3_5hmi" $pioCrow
  Build-Env "crowpanel_s3_7hmi" $pioCrow
  Build-Env "crowpanel_hmi7"    $pioCrow
  Build-Env "crowpanel_hmi5"    $pioCrow
} finally { Remove-Item Env:\PLATFORMIO_CORE_DIR -ErrorAction SilentlyContinue }
Copy-Bin "crowpanel_s3_5hmi" "firmware.factory.bin" "crowpanel"      "firmware.factory.bin"
Copy-Bin "crowpanel_s3_7hmi" "firmware.factory.bin" "crowpanel-adv7" "firmware.factory.bin"
Copy-Bin "crowpanel_hmi7"    "firmware.factory.bin" "crowpanel-hmi7" "firmware.factory.bin"
Copy-Bin "crowpanel_hmi5"    "firmware.factory.bin" "crowpanel-hmi5" "firmware.factory.bin"

Write-Host "`n--- refreshed flasher bins ---"
Write-Host ("cyd2usb        = {0} bytes" -f (Get-Item (Join-Path $flash 'cyd2usb\firmware.bin')).Length)
Write-Host ("elegoo         = {0} bytes" -f (Get-Item (Join-Path $flash 'elegoo\firmware.bin')).Length)
Write-Host ("crowpanel      = {0} bytes" -f (Get-Item (Join-Path $flash 'crowpanel\firmware.factory.bin')).Length)
Write-Host ("crowpanel-adv7 = {0} bytes" -f (Get-Item (Join-Path $flash 'crowpanel-adv7\firmware.factory.bin')).Length)
Write-Host ("crowpanel-hmi5 = {0} bytes" -f (Get-Item (Join-Path $flash 'crowpanel-hmi5\firmware.factory.bin')).Length)
Write-Host "`nNext: git add -f docs/flash; git commit"
