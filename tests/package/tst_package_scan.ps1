[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$packageScript = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'scripts/package.ps1'
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    $packageScript, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw 'The package script cannot be parsed.' }
$scan = $ast.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq 'Assert-NoPrivateFiles'
}, $true)
if (-not $scan) { throw 'The package private-file scan is missing.' }
. ([scriptblock]::Create($scan.Extent.Text))

$tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $tempRoot ("heartgold-package-scan-{0}" -f [guid]::NewGuid().ToString('N'))))
if (-not $fixtureRoot.StartsWith($tempRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
    -not (Split-Path -Leaf $fixtureRoot).StartsWith('heartgold-package-scan-')) {
    throw 'Invalid test fixture directory.'
}
$failures = [Collections.Generic.List[string]]::new()
$checks = 0

function Check-Rejected([string]$caseName, [string]$directory) {
    $script:checks++
    try { Assert-NoPrivateFiles $directory }
    catch {
        if ($_.Exception.Message -eq 'Protected/private files found in package.') {
            Write-Output "PASS $caseName"
            return
        }
        throw
    }
    $script:failures.Add($caseName)
    Write-Output "FAIL $caseName : forbidden fixture escaped the scan"
}

try {
    New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
    $hiddenFileDir = New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'hidden-file')
    $hiddenFile = Join-Path $hiddenFileDir 'fixture.sav'
    Set-Content -LiteralPath $hiddenFile -Value 'synthetic fixture'
    [IO.File]::SetAttributes($hiddenFile, [IO.FileAttributes]::Hidden)

    $systemFileDir = New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'system-file')
    $systemFile = Join-Path $systemFileDir 'fixture.nds'
    Set-Content -LiteralPath $systemFile -Value 'synthetic fixture'
    [IO.File]::SetAttributes($systemFile, [IO.FileAttributes]::System)

    $hiddenSystemDir = New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'hidden-system-file')
    $hiddenSystemFile = Join-Path $hiddenSystemDir 'fixture.rom'
    Set-Content -LiteralPath $hiddenSystemFile -Value 'synthetic fixture'
    [IO.File]::SetAttributes($hiddenSystemFile,
        [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System)

    $nestedDir = New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'hidden-directory')
    $hiddenDir = New-Item -ItemType Directory -Path (Join-Path $nestedDir 'concealed')
    Set-Content -LiteralPath (Join-Path $hiddenDir 'fixture.bin') -Value 'synthetic fixture'
    [IO.File]::SetAttributes($hiddenDir.FullName, [IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::Directory)

    foreach ($phase in 'pre-scan', 'final-scan') {
        Check-Rejected "$phase hidden file" $hiddenFileDir.FullName
        Check-Rejected "$phase system file" $systemFileDir.FullName
        Check-Rejected "$phase hidden and system file" $hiddenSystemDir.FullName
        Check-Rejected "$phase hidden directory" $nestedDir.FullName
    }
    $checks++
    $cleanDir = New-Item -ItemType Directory -Path (Join-Path $fixtureRoot 'clean')
    Set-Content -LiteralPath (Join-Path $cleanDir 'fixture.txt') -Value 'synthetic fixture'
    Assert-NoPrivateFiles $cleanDir.FullName
    Write-Output 'PASS clean directory'

    if ($failures.Count -ne 0) { throw "$($failures.Count) of $checks checks failed: $($failures -join ', ')" }
    Write-Output "Package scan: $checks checks passed"
} finally {
    if (Test-Path -LiteralPath $fixtureRoot) {
        Get-ChildItem -LiteralPath $fixtureRoot -Recurse -Force | ForEach-Object {
            $_.Attributes = $_.Attributes -band (-bnot ([IO.FileAttributes]::Hidden -bor [IO.FileAttributes]::System))
        }
        Remove-Item -LiteralPath $fixtureRoot -Recurse -Force
    }
}
