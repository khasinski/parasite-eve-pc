param([string]$BuildDirectory)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (!$BuildDirectory) { $BuildDirectory = Join-Path $root 'build-mods' }
$cache = [IO.File]::ReadAllText((Join-Path $BuildDirectory 'CMakeCache.txt'))
$match = [regex]::Match($cache, '(?m)^CMAKE_C_COMPILER:(?:STRING|FILEPATH)=(.+)\r?$')
if (!$match.Success) { throw 'Configure the native CMake build first.' }
$compiler = $match.Groups[1].Value.Trim()
# PE_RAND_MOVE_KEYS 1 is the shipped build (golden layouts); 0 keeps the keys-stay path tested.
foreach ($moveKeys in 0, 1) {
    $output = Join-Path $BuildDirectory "test-pe-mod-randomizer-keys$moveKeys.exe"
    $compileArgs = @('-std=c11', '-O1', '-Wall', '-Wextra', "-DPE_RAND_MOVE_KEYS=$moveKeys",
                     '-I', (Join-Path $root 'mods/pe_randomizer'))
    foreach ($path in @('tests/pe_mod_randomizer_test.c', 'mods/pe_randomizer/pe_rand_gen.c')) {
        $compileArgs += Join-Path $root $path
    }
    $compileArgs += '-o'; $compileArgs += $output
    & $compiler @compileArgs
    if ($LASTEXITCODE -ne 0) { throw 'Randomizer test compilation failed.' }
    & $output
    if ($LASTEXITCODE -ne 0) { throw "Randomizer regression test failed (PE_RAND_MOVE_KEYS=$moveKeys)." }
}
