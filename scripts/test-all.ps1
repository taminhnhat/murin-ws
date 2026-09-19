# Uses the same Python implementation and options as the .sh wrapper.
$ErrorActionPreference = "Stop"

# Prefer an active virtual environment; fall back to the Windows Python launcher.
if (Get-Command python -ErrorAction SilentlyContinue) {
    & python "$PSScriptRoot/tools/test-all.py" @args
} elseif (Get-Command py -ErrorAction SilentlyContinue) {
    & py -3 "$PSScriptRoot/tools/test-all.py" @args
} elseif (Get-Command python3 -ErrorAction SilentlyContinue) {
    & python3 "$PSScriptRoot/tools/test-all.py" @args
} else {
    throw "Python 3 is required. Install Python and add it to PATH."
}
exit $LASTEXITCODE
