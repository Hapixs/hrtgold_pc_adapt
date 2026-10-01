[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $projectRoot 'scripts/bootstrap.ps1'), [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -ne 0) { throw 'Cannot parse bootstrap script.' }
$shellBody = $ast.Find({
    param($node)
    $node -is [System.Management.Automation.Language.StringConstantExpressionAst] -and
        $node.Value.Contains('pacman -Syu')
}, $true)
if (-not $shellBody) { throw 'Cannot locate dependency installation shell script.' }

# Execute the real shell body, replacing only the external package manager.
# No system update, installation, network access or fixture files are involved.
$failingPacman = @'
pacman() {
  printf '%s\n' "$1"
  if [ "$1" = "-Syu" ]; then return 23; fi
  return 0
}
'@
$output = & 'C:\msys64\usr\bin\bash.exe' -c ($failingPacman + "`n" + $shellBody.Value)
$result = $LASTEXITCODE
if ($result -ne 23 -or @($output).Count -ne 1 -or $output -ne '-Syu') {
    throw "Failed update must exit 23 before installing packages; exit=$result, calls=$($output -join ',')"
}
Write-Output 'PASS failed update stops before package installation (exit 23)'

$successfulPacman = @'
pacman() {
  printf '%s\n' "$1"
  return 0
}
'@
$output = & 'C:\msys64\usr\bin\bash.exe' -c ($successfulPacman + "`n" + $shellBody.Value)
if ($LASTEXITCODE -ne 0 -or ($output -join ',') -ne '-Syu,-S') {
    throw 'Successful update must proceed to package installation.'
}
Write-Output 'PASS successful update proceeds to package installation (exit 0)'
