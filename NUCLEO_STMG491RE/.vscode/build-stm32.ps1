param(
    [ValidateSet('FLASH', 'RAM')]
    [string]$Target = 'FLASH'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$gcc = 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin\arm-none-eabi-gcc.exe'
$scriptSources = @(
    Get-ChildItem (Join-Path $root 'Core\Src\*.c')
    Get-ChildItem (Join-Path $root 'Core\Startup\*.s')
    Get-ChildItem (Join-Path $root 'Core\APPS\PRESSURE_SENSOR\*.c')
    Get-ChildItem (Join-Path $root 'Drivers\STM32G4xx_HAL_Driver\Src\*.c')
    Get-ChildItem (Join-Path $root 'Drivers\BSP\STM32G4xx_Nucleo\*.c')
)
$sources = @($scriptSources | ForEach-Object { $_.FullName })
$buildDirectory = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null
$linkerScript = Join-Path $root "STM32G491RETX_${Target}.ld"
$targetName = $Target.ToLower()
$outputBase = Join-Path $buildDirectory "output_$targetName"

& $gcc -g -mcpu=cortex-m4 -mthumb '--specs=nano.specs' $sources `
    "-I$(Join-Path $root 'Core\Inc')" `
    "-I$(Join-Path $root 'Core\APPS\PRESSURE_SENSOR')" `
    "-I$(Join-Path $root 'Drivers\STM32G4xx_HAL_Driver\Inc')" `
    "-I$(Join-Path $root 'Drivers\STM32G4xx_HAL_Driver\Inc\Legacy')" `
    "-I$(Join-Path $root 'Drivers\CMSIS\Device\ST\STM32G4xx\Include')" `
    "-I$(Join-Path $root 'Drivers\CMSIS\Include')" `
    "-I$(Join-Path $root 'Drivers\BSP\STM32G4xx_Nucleo')" `
    '-DDEBUG' '-DUSE_NUCLEO_64' '-DUSE_HAL_DRIVER' '-DSTM32G491xx' `
    '-Wl,--gc-sections' "-T$linkerScript" "-Wl,-Map=$outputBase.map" '-o' "$outputBase.elf"

if ($LASTEXITCODE -ne 0) {
    Write-Host "BUILD FAILED: $Target target" -ForegroundColor Red
    exit $LASTEXITCODE
}

Write-Host "BUILD SUCCESSFUL: $Target target" -ForegroundColor Green
Write-Host "ELF: $outputBase.elf"
Write-Host "MAP: $outputBase.map"
