# The Python helper discovers and prompts; this only bootstraps it.
$ErrorActionPreference = 'Stop'
$bootstrap = Get-Command python -ErrorAction SilentlyContinue
if (-not $bootstrap) { $bootstrap = Get-Command python3 -ErrorAction SilentlyContinue }
if (-not $bootstrap) {
    Write-Error 'Python is not on PATH. Run a Python >= 3.9 executable with scripts/install.py.'
    exit 2
}
& $bootstrap.Source (Join-Path $PSScriptRoot 'install.py') @args
exit $LASTEXITCODE
