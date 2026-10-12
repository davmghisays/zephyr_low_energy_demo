param(
    [switch]$SensorAlwaysOn
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
    if ($SensorAlwaysOn) {
        $CmakeArgs += "-DCONFIG_DEMO_SENSOR_POWER_ALWAYS_ON=y"
    }

    & $WestExe build -p always -b nucleo_wl55jc $ProjectDir -d $BuildDir -- $CmakeArgs
    if ($LASTEXITCODE -ne 0) {
        throw "west build termino con codigo $LASTEXITCODE"
    }
}
finally {
    Pop-Location
}
