<#
.SYNOPSIS
    Run the jh host CLI of this checkout, building it first when its inputs
    changed (scripts\jh_bootstrap.cmake).

.DESCRIPTION
    Needs CMake 3.22+, Git and Visual Studio 2022 (or cl.exe in PATH with the
    Ninja generator). The managed checkouts come from the component manager.
    Works in Windows PowerShell 5.1 and PowerShell 7. The environment variables
    JH_BOOTSTRAP_CMAKE, JH_DEPENDENCY_ROOT, JH_OUTPUT_ROOT and JH_JHBASE_DIR
    reach the bootstrap as for scripts/jh.

.PARAMETER Arch
    Process architecture of jh on Windows x64: x64 (default) or x86.

.PARAMETER Generator
    CMake generator for the tool build; empty selects Visual Studio.

.PARAMETER Arguments
    Everything else is passed to jh unchanged.

.EXAMPLE
    scripts\jh.ps1 --version --json
    scripts\jh.ps1 -Arch x86 generate device-defaults --root . --check
#>
# Only -Arch and -Generator are parameters of the launcher; every other word,
# including command names such as "generate", belongs to jh.
[CmdletBinding(PositionalBinding = $false)]
param(
    [ValidateSet('x64', 'x86')]
    [string]$Arch = 'x64',
    [string]$Generator = '',
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Arguments = @()
)

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Bootstrap = Join-Path $PSScriptRoot 'jh_bootstrap.cmake'
$CMake = if ($env:JH_BOOTSTRAP_CMAKE) { $env:JH_BOOTSTRAP_CMAKE } else { 'cmake' }
if (-not (Get-Command $CMake -ErrorAction SilentlyContinue)) {
    [Console]::Error.WriteLine('jh: cmake is required to build the host CLI; install CMake 3.22 or newer')
    exit 3
}
$Result = Join-Path ([IO.Path]::GetTempPath()) ('jh-bootstrap-' + [Guid]::NewGuid().ToString('N') + '.txt')
try {
    # Progress goes to the console as error output; stdout stays with the tool.
    & $CMake "-DJH_ROOT=$RepoRoot" "-DJH_ARCH=$Arch" "-DJH_GENERATOR=$Generator" `
        "-DJH_RESULT_FILE=$Result" -P $Bootstrap 2>&1 | ForEach-Object { [Console]::Error.WriteLine("$_") }
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
    # CMake writes the result as UTF-8; Windows PowerShell 5.1 would read it
    # as ANSI and lose a non-ASCII path.
    $Executable = (Get-Content -LiteralPath $Result -Raw -Encoding UTF8).Trim()
} finally {
    Remove-Item -LiteralPath $Result -ErrorAction SilentlyContinue
}
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    [Console]::Error.WriteLine("jh: the bootstrap published no executable at $Executable")
    exit 3
}
& $Executable @Arguments
exit $LASTEXITCODE
