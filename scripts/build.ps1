[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('ucrt-debug', 'ucrt-release')]
    [string]$Preset,

    [string]$Test
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmake = 'C:\msys64\ucrt64\bin\cmake.exe'
$ctest = 'C:\msys64\ucrt64\bin\ctest.exe'
$buildDirectory = Join-Path $projectRoot "build/$Preset"

if (-not (Test-Path -LiteralPath $cmake)) {
    throw "CMake was not found at $cmake. Run ./scripts/bootstrap.ps1 first."
}

$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"

& $cmake --preset $Preset --fresh
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $cmake --build $buildDirectory
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$ctestArguments = @('--test-dir', $buildDirectory, '--output-on-failure')
if ($Test) {
    $ctestArguments += @('-R', $Test)
}

& $ctest @ctestArguments
exit $LASTEXITCODE
