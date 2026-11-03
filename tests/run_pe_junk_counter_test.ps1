param([string]$BuildDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (!$BuildDirectory) { $BuildDirectory=Join-Path $root 'build-mods' }
$cache=[IO.File]::ReadAllText((Join-Path $BuildDirectory 'CMakeCache.txt'))
$match=[regex]::Match($cache,'(?m)^CMAKE_C_COMPILER:(?:STRING|FILEPATH)=(.+)\r?$')
if (!$match.Success) { throw 'Configure the native CMake build first.' }
$compiler=$match.Groups[1].Value.Trim()
$output=Join-Path $BuildDirectory 'test-pe-mod-junk-counter.exe'
$compileArgs=@('-D__psyz','-DPE_MODS=1','-std=c11','-O1','-fno-strict-aliasing','-Wno-pointer-sign')
foreach ($path in @('include','mods/host','mods/pe_economy','mods/pe_catalog','third_party/psyz/psyz/include','third_party/psyz/external/SDL/include')) {
    $compileArgs+='-I'; $compileArgs+=Join-Path $root $path
}
$compileArgs+=Join-Path $root 'tests/pe_mod_junk_counter_test.c'
$compileArgs+=Join-Path $root 'mods/pe_economy/pe_junk_counter.c'
$compileArgs+='-o'; $compileArgs+=$output
& $compiler @compileArgs
if ($LASTEXITCODE -ne 0) { throw 'Junk counter test compilation failed.' }
& $output
if ($LASTEXITCODE -ne 0) { throw 'Junk counter regression test failed.' }
