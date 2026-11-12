param(
    [string]$PsyzCheckout,
    [string]$DependencyCache,
    [string]$Compiler = 'clang',
    [int]$Jobs = 8
)
$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$dependencyLink = Join-Path $projectRoot 'third_party\psyz'
$buildDirectory = Join-Path $projectRoot 'build\bp-ui-headless'
$createdLink = $false
try {
    if (-not (Test-Path (Join-Path $dependencyLink 'psyz\CMakeLists.txt'))) {
        if (-not $PsyzCheckout) { throw 'Initialize the PsyZ submodule, or pass -PsyzCheckout pointing to an existing local checkout.' }
        $dependencyTarget = (Resolve-Path $PsyzCheckout).Path
        if (-not (Test-Path (Join-Path $dependencyTarget 'psyz\CMakeLists.txt'))) { throw 'PsyzCheckout is not a PsyZ checkout.' }
        if ((Get-ChildItem -LiteralPath $dependencyLink -Force | Measure-Object).Count) { throw 'Refusing to replace a nonempty dependency directory.' }
        Remove-Item -LiteralPath $dependencyLink
        New-Item -ItemType Junction -Path $dependencyLink -Target $dependencyTarget | Out-Null
        $createdLink = $true
    }
    $configure = @('-S', $projectRoot, '-B', $buildDirectory, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=RelWithDebInfo', "-DCMAKE_C_COMPILER=$Compiler",
        '-DPE_MODS=ON', '-DPE_MOD_LIST=pe_economy;pe_shops;pe_bp', '-DBUILD_TESTING=OFF',
        "-DCMAKE_PROJECT_parasite-eve-pc_INCLUDE=$PSScriptRoot/headless_inject.cmake")
    if ($DependencyCache) {
        $cacheRoot = (Resolve-Path $DependencyCache).Path
        foreach ($package in @('pe_json','pe_zip','pe_curl','psyz_webp')) {
            $sourcePath = Join-Path $cacheRoot "$package-src"
            if (Test-Path $sourcePath) { $configure += "-DFETCHCONTENT_SOURCE_DIR_$($package.ToUpperInvariant())=$sourcePath" }
        }
        $configure += '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'
    }
    & cmake @configure
    if ($LASTEXITCODE) { throw "Configure failed: $LASTEXITCODE" }
    & cmake --build $buildDirectory --target parasite-eve-pc -j $Jobs
    if ($LASTEXITCODE) { throw "Build failed: $LASTEXITCODE" }
} finally {
    if ($createdLink) {
        $item = Get-Item -LiteralPath $dependencyLink -Force
        if ($item.LinkType -ne 'Junction' -or $item.FullName -ne $dependencyLink) { throw 'Dependency link changed; refusing cleanup.' }
        # Remove only the junction, never its target or a populated directory.
        Remove-Item -LiteralPath $dependencyLink
        New-Item -ItemType Directory -Path $dependencyLink | Out-Null
    }
}
