<#
.SYNOPSIS
Formats C/C++ source files in main and tests, Python source files in the
project, and CMake files.

.DESCRIPTION
Uses the repository's .clang-format configuration for C/C++ files, Ruff for
Python files under tests, tools, and utils, and cmake-format for CMake files.
Generated and vendored directories are excluded from Python and CMake scans.

.NOTES
Required tools: clang-format, ruff, and cmake-format.
#>

$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$mainRoot = Join-Path $repositoryRoot 'main'
$testsRoot = Join-Path $repositoryRoot 'tests'
$clangFormat = Get-Command clang-format -ErrorAction SilentlyContinue

if ($null -eq $clangFormat) {
    throw 'clang-format was not found on PATH.'
}

# Format project and test C/C++ source and header files. Generated test build
# trees are excluded because they contain vendored GoogleTest sources.
$extensions = @('*.c', '*.h', '*.cc', '*.cpp', '*.cxx', '*.hh', '*.hpp', '*.hxx')
$clangRoots = @($mainRoot, $testsRoot) | Where-Object { Test-Path $_ }
$files = $clangRoots |
    ForEach-Object {
        Get-ChildItem -Path $_ -Recurse -File -Include $extensions -ErrorAction SilentlyContinue
    } |
    Where-Object {
        $_.FullName -notmatch '[\\/](build[^\\/]*|\.venv|__pycache__|node_modules|managed_components|\.git|\.pytest_cache)[\\/]'
    }

foreach ($file in $files) {
    Write-Host "Formatting $($file.FullName)"
    & $clangFormat.Source -i --style=file $file.FullName
    if ($LASTEXITCODE -ne 0) {
        throw "clang-format failed for $($file.FullName)"
    }
}

Write-Host "Formatted $($files.Count) C/C++ file(s) under main and tests."

# Format Python files in the project utility and test directories.
$pythonRoots = @('tests', 'tools', 'utils') |
    ForEach-Object { Join-Path $repositoryRoot $_ } |
    Where-Object { Test-Path $_ }
$ruff = Get-Command ruff -ErrorAction SilentlyContinue

if ($null -eq $ruff) {
    throw 'ruff was not found on PATH; it is required to format Python files.'
}

$pythonFiles = $pythonRoots |
    ForEach-Object {
        Get-ChildItem -Path $_ -Recurse -File -Filter '*.py' -ErrorAction SilentlyContinue
    } |
    Where-Object {
        $_.FullName -notmatch '[\\/](build[^\\/]*|\.venv|__pycache__|\.pytest_cache)[\\/]'
    }

if ($pythonFiles.Count -gt 0) {
    Write-Host "Formatting $($pythonFiles.Count) Python file(s) under tests, tools, and utils."
    & $ruff.Source format $pythonFiles.FullName
    if ($LASTEXITCODE -ne 0) {
        throw 'ruff format failed.'
    }
}

# Format project CMake files, excluding generated and vendored content.
$cmakeFormat = Get-Command cmake-format -ErrorAction SilentlyContinue

if ($null -eq $cmakeFormat) {
    throw 'cmake-format was not found on PATH; install cmake-format to format CMake files.'
}

$cmakeFiles = Get-ChildItem -Path $repositoryRoot -Recurse -File `
    -Include 'CMakeLists.txt', '*.cmake' -ErrorAction SilentlyContinue |
    Where-Object {
        $_.FullName -notmatch '[\\/](build[^\\/]*|\.venv|node_modules|managed_components|\.git|\.pytest_cache)[\\/]'
    }

if ($cmakeFiles.Count -gt 0) {
    Write-Host "Formatting $($cmakeFiles.Count) CMake file(s)."
    foreach ($file in $cmakeFiles) {
        & $cmakeFormat.Source -i $file.FullName
        if ($LASTEXITCODE -ne 0) {
            throw "cmake-format failed for $($file.FullName)"
        }
    }
}
