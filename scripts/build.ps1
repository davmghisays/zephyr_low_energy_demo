param(
    [ValidateSet("high-energy", "sensor-managed", "system-managed", "runtime-system", "low-energy")]
    [string]$Profile = "low-energy"
)

$ErrorActionPreference = "Stop"

$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ZephyrDir = "C:\ZephyrUPM\zephyrproject-4.4\zephyr"
$WestExe = "C:\ZephyrUPM\zephyrproject-4.4\.venv\Scripts\west.exe"
$SdkDir = "C:\Users\davma\zephyr-sdk-1.0.1"

$env:ZEPHYR_BASE = $ZephyrDir
$env:ZEPHYR_TOOLCHAIN_VARIANT = "zephyr"
$env:ZEPHYR_SDK_INSTALL_DIR = $SdkDir

$BuildDir = Join-Path $ProjectDir "build"

Write-Host "Build: $BuildDir"

Push-Location $ZephyrDir
try {
    $CmakeArgs = @("-DDTC_OVERLAY_FILE=boards/nucleo_wl55jc.overlay")
    $ExtraConfFile = switch ($Profile) {
        "high-energy" { "confs/prj_high_energy.conf" }
        "sensor-managed" { "confs/prj_sensor_managed.conf" }
        "system-managed" { "confs/prj_system_managed.conf" }
        "runtime-system" { "confs/prj_runtime_system.conf" }
        "low-energy" { "confs/prj_low_energy.conf" }
    }

    $CmakeArgs += "-DEXTRA_CONF_FILE=$ExtraConfFile"

    & $WestExe build -p always -b nucleo_wl55jc $ProjectDir -d $BuildDir -- $CmakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "west build termino con codigo $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
