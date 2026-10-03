$ErrorActionPreference = "Stop"

$ProjectDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Programmer = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
$HexFile = Join-Path $ProjectDir "build\zephyr\zephyr.hex"

if (-not (Test-Path -LiteralPath $Programmer)) {
    throw "No se encuentra STM32_Programmer_CLI.exe"
}
if (-not (Test-Path -LiteralPath $HexFile)) {
    throw "No existe $HexFile. Compile primero con scripts\build.ps1."
}

& $Programmer --connect port=swd mode=UR reset=HWrst --download $HexFile --start
if ($LASTEXITCODE -ne 0) {
    throw "El flasheo termino con codigo $LASTEXITCODE"
}
