param([string]$BuildDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (!$BuildDirectory) { $BuildDirectory=Join-Path $root 'build-mods' }
$cache=[IO.File]::ReadAllText((Join-Path $BuildDirectory 'CMakeCache.txt'))
$match=[regex]::Match($cache,'(?m)^CMAKE_C_COMPILER:(?:STRING|FILEPATH)=(.+)\r?$')
if (!$match.Success) { throw 'Configure the native CMake build first.' }
$compiler=$match.Groups[1].Value.Trim()
$output=Join-Path $BuildDirectory 'test-pe-mod-armor-effect-display.exe'
$compileArgs=@('-D__psyz','-DPE_MODS=1','-std=c11','-O1','-fno-strict-aliasing','-Wno-pointer-sign')
foreach ($path in @('include','mods/host','mods/pe_economy','mods/pe_catalog','third_party/psyz/psyz/include','third_party/psyz/external/SDL/include')) {
    $compileArgs+='-I'; $compileArgs+=Join-Path $root $path
}
$compileArgs+=Join-Path $root 'tests/pe_mod_armor_effect_display_test.c'
$compileArgs+=Join-Path $root 'mods/pe_catalog/pe_armor_effect_display.c'
$compileArgs+='-o'; $compileArgs+=$output
& $compiler @compileArgs
if ($LASTEXITCODE -ne 0) { throw 'Armor effect display test compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Armor effect display regression test failed.' }
