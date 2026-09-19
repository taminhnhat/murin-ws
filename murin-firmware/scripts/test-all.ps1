<#
.SYNOPSIS
Runs pytest, GoogleTest, and LLVM coverage analysis.

.DESCRIPTION
Forwards all arguments to scripts/test-all.py.
#>

$ErrorActionPreference = 'Stop'
python "$PSScriptRoot\test-all.py" @args
exit $LASTEXITCODE
