# Authenticate with 1Password, inject only the credentials needed by the
# selected release phase, and run it locally. Windows counterpart of
# release/pixelview-macos.sh. Usage:
#   powershell -File release\pixelview-windows.ps1 --validate-config|--prepare|--publish|--publish-latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$releaseScript = Join-Path $root 'cmake\windows\pixelview-release.py'
$configFile = Join-Path $root 'release\windows.env'
$r2File = Join-Path $root 'release\windows-r2.1password.env'
$keyFile = Join-Path $root 'release\windows-update-key.1password.env'

$mode = $args | Where-Object { $_ -in '--prepare', '--publish', '--publish-latest', '--validate-config' } | Select-Object -Last 1
switch ($mode) {
  '--prepare' { $secretFiles = @($keyFile) }
  '--publish' { $secretFiles = @($r2File) }
  '--publish-latest' { $secretFiles = @($r2File) }
  default {
    & python $releaseScript @args
    exit $LASTEXITCODE
  }
}

if (-not (Get-Command op -ErrorAction SilentlyContinue)) {
  Write-Error '1Password CLI (op) is required'
  exit 2
}
foreach ($file in @($configFile) + $secretFiles) {
  if (-not (Test-Path $file)) { Write-Error "missing release environment file: $file"; exit 2 }
}
$envArgs = @('--env-file', $configFile)
foreach ($file in $secretFiles) { $envArgs += @('--env-file', $file) }
# Values exist only in the python subprocess and are masked if printed.
& op run @envArgs -- python $releaseScript @args
exit $LASTEXITCODE
