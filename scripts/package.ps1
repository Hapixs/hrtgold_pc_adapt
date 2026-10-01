[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$distRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))
$dist = [IO.Path]::GetFullPath((Join-Path $distRoot 'HeartGoldPCRemastered'))
$toolchain = 'C:\msys64\ucrt64'
$bin = Join-Path $toolchain 'bin'
$oldPath = $env:PATH
$oldLocation = Get-Location

function Check-Exit([string]$operation) {
    if ($LASTEXITCODE -ne 0) { throw "$operation failed (exit $LASTEXITCODE)." }
}
function Assert-NoPrivateFiles([string]$directory) {
    $forbidden = Get-ChildItem -LiteralPath $directory -Recurse -Force -File | Where-Object {
        $_.Extension -in '.nds','.sav','.dsv','.bin','.rom'
    }
    if ($forbidden) { throw 'Protected/private files found in package.' }
}

try {
    Set-Location -LiteralPath $projectRoot
    $env:PATH = "$bin;$env:PATH"
    & (Join-Path $PSScriptRoot 'build.ps1') -Preset ucrt-release
    Check-Exit 'Release build and CTest'

    # Only this resolved local artifact directory may be replaced. Refuse links
    # so an unexpected junction cannot redirect recursive removal elsewhere.
    if ($dist -ne (Join-Path $distRoot 'HeartGoldPCRemastered') -or
        -not $dist.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Invalid package destination.'
    }
    if (Test-Path -LiteralPath $distRoot) {
        if ((Get-Item -LiteralPath $distRoot).Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw 'The dist directory must not be a link.'
        }
    }
    if (Test-Path -LiteralPath $dist) {
        $links = @((Get-Item -LiteralPath $dist)) + @(Get-ChildItem -LiteralPath $dist -Recurse -Force)
        if ($links | Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
            throw 'The package directory must not contain links.'
        }
        Assert-NoPrivateFiles $dist
        Remove-Item -LiteralPath $dist -Recurse -Force
    }
    New-Item -ItemType Directory -Path $dist -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $projectRoot 'build/ucrt-release/heartgold_pc.exe') -Destination $dist
    & (Join-Path $bin 'windeployqt6.exe') --release --no-translations --qmldir (Join-Path $projectRoot 'src/ui/qml') --dir $dist (Join-Path $dist 'heartgold_pc.exe')
    Check-Exit 'Qt deployment'
    Copy-Item -LiteralPath (Join-Path $bin 'SDL3.dll') -Destination $dist

    # Walk actual PE imports for every plugin, QML library and runtime DLL.
    # Windows system libraries remain supplied by the operating system.
    $queue = [Collections.Generic.Queue[string]]::new()
    Get-ChildItem -LiteralPath $dist -Recurse -Force -File | Where-Object { $_.Extension -in '.exe','.dll' } | ForEach-Object { $queue.Enqueue($_.FullName) }
    $visited = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    while ($queue.Count -gt 0) {
        $binary = $queue.Dequeue()
        if (-not $visited.Add($binary)) { continue }
        $imports = & (Join-Path $bin 'objdump.exe') -p $binary
        Check-Exit 'Dependency inspection'
        foreach ($line in $imports) {
            if ($line -notmatch 'DLL Name:\s*(.+?)\s*$') { continue }
            $dll = $Matches[1]
            $source = Join-Path $bin $dll
            $destination = Join-Path $dist $dll
            if (Test-Path -LiteralPath $source) {
                if (-not (Test-Path -LiteralPath $destination)) {
                    Copy-Item -LiteralPath $source -Destination $destination
                    $queue.Enqueue($destination)
                }
            } elseif (-not (Test-Path -LiteralPath (Join-Path "$env:SystemRoot/System32" $dll)) -and
                      $dll -notmatch '^(api-ms-win-|ext-ms-win-)') {
                throw "Unresolved package dependency: $dll"
            }
        }
    }
    Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSES') -Destination $dist -Recurse
    Copy-Item -LiteralPath (Join-Path $projectRoot 'THIRD_PARTY.md') -Destination $dist
    Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md') -Destination $dist
    Copy-Item -LiteralPath (Join-Path $toolchain 'share/licenses') -Destination (Join-Path $dist 'LICENSES/UCRT64') -Recurse
    Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/melonds/LICENSE') -Destination (Join-Path $dist 'LICENSES/melonDS.txt')
    Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/melonds/src/teakra/LICENSE') -Destination (Join-Path $dist 'LICENSES/Teakra.txt')
    '[Paths]', 'Prefix=.', 'Plugins=.', 'QmlImports=qml' | Set-Content -LiteralPath (Join-Path $dist 'qt.conf') -Encoding ascii
    Assert-NoPrivateFiles $dist
    Write-Output "Package: $dist"
    Write-Output 'Forbidden-file scan: 0 matches'
    Write-Output ("Packaged PE files inspected: {0}" -f $visited.Count)
} finally {
    $env:PATH = $oldPath
    Set-Location -LiteralPath $oldLocation
}
