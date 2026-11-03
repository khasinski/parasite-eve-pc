param([string]$BuildDirectory)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if (!$BuildDirectory) { $BuildDirectory=Join-Path $root 'build-mods' }
$cache=[IO.File]::ReadAllText((Join-Path $BuildDirectory 'CMakeCache.txt'))
$match=[regex]::Match($cache,'(?m)^CMAKE_C_COMPILER:(?:STRING|FILEPATH)=(.+)\r?$')
if (!$match.Success) { throw 'Configure the native CMake build first.' }
$compiler=$match.Groups[1].Value.Trim()
$output=Join-Path $BuildDirectory 'test-pe-mod-collection.exe'
$compileArgs=@('-D__psyz','-DPE_MODS=1','-std=c11','-O1','-fno-strict-aliasing','-Wno-pointer-sign')
foreach ($path in @('include','mods/host','mods/pe_collection','mods/pe_wardrobe','third_party/psyz/psyz/include','third_party/psyz/external/SDL/include')) {
    $compileArgs+='-I'; $compileArgs+=Join-Path $root $path
}
$compileArgs+=Join-Path $root 'tests/pe_mod_collection_test.c'
foreach ($file in @('pe_collection.c','pe_profile.c','pe_bestiary.c','pe_pack_pe1.c')) {
    $compileArgs+=Join-Path $root "mods/pe_collection/$file"
}
foreach ($file in @('pe_wardrobe_mod.c','pe_wardrobe_profile.c','pe_wardrobe.c','pe_pack_pe1_costumes.c')) {
    $compileArgs+=Join-Path $root "mods/pe_wardrobe/$file"
}
$compileArgs+='-o'; $compileArgs+=$output
& $compiler @compileArgs
if ($LASTEXITCODE -ne 0) { throw 'Collection test compilation failed.' }
$env:PE_TEST_DIR=$BuildDirectory
& $output
if ($LASTEXITCODE -ne 0) { throw 'Collection regression test failed.' }
