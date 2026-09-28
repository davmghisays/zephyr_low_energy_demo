param(
    [Parameter(Mandatory = $true)]
    [ValidateSet(
        "diagnostic",
        "verify-reference",
        "verify-response",
        "verify-saving",
        "reference",
        "response",
        "saving",
        "saving-fast"
    )]
    [string]$Profile
)

$ErrorActionPreference = "Stop"

$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ZephyrDir = "C:\ZephyrUPM\zephyrproject-4.4\zephyr"
$WestExe = "C:\ZephyrUPM\zephyrproject-4.4\.venv\Scripts\west.exe"
$SdkDir = "C:\Users\davma\zephyr-sdk-1.0.1"

$env:ZEPHYR_BASE = $ZephyrDir
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = $SdkDir

$Mode = switch -Wildcard ($Profile) {
    "*reference" { "reference"; break }
    "*response"  { "response"; break }
    default       { "saving" }
}

$OutputConfig = if ($Profile -eq "diagnostic" -or $Profile.StartsWith("verify-")) {
    "confs/diagnostic.conf"
} else {
    "confs/measurement.conf"
}

$ExtraConfigs = @($OutputConfig, "confs/mode_$Mode.conf")
if ($Profile -eq "saving-fast") {
    $ExtraConfigs += "confs/interval_fast.conf"
}

$BuildDir = Join-Path $ProjectDir "build-$Profile"
$ExtraConfigArg = $ExtraConfigs -join ";"

Write-Host "Profile: $Profile"
Write-Host "Mode: $Mode"
Write-Host "Extra config: $ExtraConfigArg"
Write-Host "Build: $BuildDir"

Push-Location $ZephyrDir
try {
    & $WestExe build -p always -b nucleo_wl55jc $ProjectDir -d $BuildDir -- `
        "-DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay" `
        "-DEXTRA_CONF_FILE=$ExtraConfigArg"
    if ($LASTEXITCODE -ne 0) {
        throw "west build termino con codigo $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
