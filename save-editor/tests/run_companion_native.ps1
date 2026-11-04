param([string]$Compiler='clang', [string]$Cargo='cargo', [string]$BuildDirectory)
$ErrorActionPreference='Stop'
$editor=Split-Path -Parent $PSScriptRoot
$game=Split-Path -Parent $editor
if (!$BuildDirectory) { $BuildDirectory=Join-Path $game 'build-mods/editor-companion-tests' }
New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
$exe=Join-Path ([IO.Path]::GetFullPath($BuildDirectory)) 'companion-native.exe'
$flags=@('-D__psyz','-DPE_MODS=1','-std=c11','-O1','-fno-strict-aliasing','-Wno-pointer-sign','-ffunction-sections','-fdata-sections','-Wl,--gc-sections')
foreach($folder in @('include','mods/host','third_party/psyz/psyz/include','third_party/psyz/external/SDL/include')) { $flags+=('-I'+(Join-Path $game $folder)) }
& $Compiler @flags (Join-Path $PSScriptRoot 'companion_native.c') (Join-Path $game 'mods/host/pe_save.c') (Join-Path $game 'mods/host/pe_extension_store.c') -o $exe
if($LASTEXITCODE) {throw 'Native companion oracle compilation failed'}
$previous=$env:PE_COMPANION_ORACLE
try {
    $env:PE_COMPANION_ORACLE=$exe
    & $Cargo test --offline --locked --manifest-path (Join-Path $editor 'Cargo.toml') native_host_accepts_published_companion -- --nocapture
    if($LASTEXITCODE) {throw 'Native companion compatibility test failed'}
} finally {$env:PE_COMPANION_ORACLE=$previous}
