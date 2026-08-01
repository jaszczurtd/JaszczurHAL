<#
.SYNOPSIS
    Prepare or verify a native Windows JaszczurHAL firmware environment.

.DESCRIPTION
    Installs pinned tools under a short user-local root, creates the managed
    Python environment, synchronizes source components, and runs the final host
    contract check. The script supports Windows PowerShell 5.1.

.PARAMETER VerifyOnly
    Verify every component without downloading, installing, or repairing it.

.PARAMETER Force
    Prefer pinned managed tools even when a compatible system tool is present.

.PARAMETER ConfigureHost
    Explicitly allow the script to set Git core.longpaths and, when already
    elevated, Windows LongPathsEnabled. The script never elevates itself.

.PARAMETER InstallExtensions
    Explicitly allow installation of missing recommended VS Code extensions.

.PARAMETER FirmwareOnly
    Verify the firmware toolchain without requiring VS Code or its extensions.
    Editor checks remain visible as optional inventory entries.
#>

[CmdletBinding()]
param(
    [switch]$VerifyOnly,
    [switch]$Force,
    [switch]$ConfigureHost,
    [switch]$InstallExtensions,
    [switch]$FirmwareOnly,
    [string]$ToolsRoot = "$env:USERPROFILE\.jh\tools",
    [string]$BuildRoot = "$env:USERPROFILE\.jh\build"
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$RepoRoot = $PSScriptRoot
$ComponentManager = Join-Path $RepoRoot 'scripts\component_manager.py'
$Inventory = Join-Path $RepoRoot 'scripts\windows_host_inventory.ps1'
$ToolConfig = Join-Path $RepoRoot 'third_party\windows_tools_version.conf'
$Requirements = Join-Path $RepoRoot 'third_party\windows_requirements.txt'
$VenvRoot = Join-Path $RepoRoot '.build\windows\venv'
$VenvPython = Join-Path $VenvRoot 'Scripts\python.exe'
$HostEnvironment = Join-Path $RepoRoot '.build\windows\host-environment.json'

function Fail {
    param([string]$Message)
    throw $Message
}

function Read-PinConfig {
    param([string]$Path)
    $values = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        $text = $line.Trim()
        if (-not $text -or $text.StartsWith('#')) { continue }
        $separator = $text.IndexOf('=')
        if ($separator -le 0) { Fail "Invalid pin line in ${Path}: $text" }
        $key = $text.Substring(0, $separator).Trim()
        $value = $text.Substring($separator + 1).Trim()
        if (($value.StartsWith('"') -and $value.EndsWith('"')) -or
            ($value.StartsWith("'") -and $value.EndsWith("'"))) {
            $value = $value.Substring(1, $value.Length - 2)
        }
        $values[$key] = $value
    }
    return $values
}

function Invoke-Checked {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$Description
    )
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        Fail "$Description failed with exit code $LASTEXITCODE."
    }
}

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-LongPathsEnabled {
    try {
        return (Get-ItemProperty `
            'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' `
            -Name LongPathsEnabled -ErrorAction Stop).LongPathsEnabled
    } catch {
        return 0
    }
}

function Prepare-HostSettings {
    $git = Get-Command git.exe -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $git) { Fail 'Git for Windows is required before running the bootstrap.' }

    $gitLongPaths = (& $git.Source config --get core.longpaths 2>$null | Out-String).Trim()
    if ($gitLongPaths -ne 'true') {
        if (-not $ConfigureHost) {
            Fail 'git core.longpaths is not true. Re-run with -ConfigureHost or run: git config --global core.longpaths true'
        }
        Invoke-Checked $git.Source @('config', '--global', 'core.longpaths', 'true') `
            'Configuring git core.longpaths'
    }

    if ((Get-LongPathsEnabled) -ne 1) {
        if (-not $ConfigureHost) {
            Fail 'Windows LongPathsEnabled is disabled. Re-run an elevated shell with -ConfigureHost after reviewing the plan.'
        }
        if (-not (Test-Administrator)) {
            Fail '-ConfigureHost was provided, but LongPathsEnabled requires an already elevated PowerShell. The script did not elevate itself.'
        }
        Set-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' `
            -Name LongPathsEnabled -Type DWord -Value 1
    }
}

function Test-ManagedPythonBase {
    param([string]$Python, [string]$Stamp, [string]$ExpectedStamp, [string]$Version)
    if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) { return $false }
    if (-not (Test-Path -LiteralPath $Stamp -PathType Leaf)) { return $false }
    if ((Get-Content -LiteralPath $Stamp -Raw).Trim() -ne $ExpectedStamp) { return $false }
    $found = (& $Python -c 'import sys; print(chr(46).join(map(str, sys.version_info[:3])))' 2>$null | Out-String).Trim()
    return ($LASTEXITCODE -eq 0 -and $found -eq $Version)
}

function Install-ManagedPythonBase {
    param(
        [hashtable]$Config,
        [string]$Destination,
        [string]$ExpectedStamp
    )
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ("jh-python-" + [guid]::NewGuid().ToString('N'))
    $archive = Join-Path $temporary 'python.nupkg'
    $staging = Join-Path $temporary 'extract'
    New-Item -ItemType Directory -Path $staging -Force | Out-Null
    try {
        Write-Host "Downloading managed Python $($Config.WINDOWS_PYTHON_VERSION)..."
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $client = New-Object Net.WebClient
        $client.DownloadFile($Config.WINDOWS_PYTHON_URL, $archive)
        $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $Config.WINDOWS_PYTHON_SHA256) {
            Fail "Managed Python checksum mismatch: expected $($Config.WINDOWS_PYTHON_SHA256), got $actual."
        }
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::ExtractToDirectory($archive, $staging)
        Set-Content -LiteralPath (Join-Path $staging '.jaszczurhal-component-version') `
            -Value $ExpectedStamp -Encoding ASCII

        $candidate = Join-Path $staging $Config.WINDOWS_PYTHON_EXECUTABLE
        $version = (& $candidate -c 'import sys; print(chr(46).join(map(str, sys.version_info[:3])))' | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or $version -ne $Config.WINDOWS_PYTHON_VERSION) {
            Fail "Extracted managed Python reported '$version'."
        }

        $parent = Split-Path -Parent $Destination
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
        $backup = "$Destination.backup.$([guid]::NewGuid().ToString('N'))"
        if (Test-Path -LiteralPath $Destination) { Move-Item -LiteralPath $Destination -Destination $backup }
        try {
            Move-Item -LiteralPath $staging -Destination $Destination
        } catch {
            if ((Test-Path -LiteralPath $backup) -and -not (Test-Path -LiteralPath $Destination)) {
                Move-Item -LiteralPath $backup -Destination $Destination
            }
            throw
        }
        if (Test-Path -LiteralPath $backup) { Remove-Item -LiteralPath $backup -Recurse -Force }
    } finally {
        if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary -Recurse -Force }
    }
}

function Test-ManagedVenv {
    param([string]$Python, [string]$Stamp, [string]$ExpectedStamp)
    if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) { return $false }
    if (-not (Test-Path -LiteralPath $Stamp -PathType Leaf)) { return $false }
    if ((Get-Content -LiteralPath $Stamp -Raw).Trim() -ne $ExpectedStamp) { return $false }
    & $Python -c 'import serial,sys; sys.exit(0 if serial.__version__ == str(3.5) else 1)' 2>$null
    return ($LASTEXITCODE -eq 0)
}

function Ensure-ManagedPython {
    $config = Read-PinConfig $ToolConfig
    $baseRoot = Join-Path $ToolsRoot ("python-" + $config.WINDOWS_PYTHON_VERSION)
    $basePython = Join-Path $baseRoot $config.WINDOWS_PYTHON_EXECUTABLE
    $baseStamp = Join-Path $baseRoot '.jaszczurhal-component-version'
    $expectedBase = "python|$($config.WINDOWS_PYTHON_VERSION)|$($config.WINDOWS_PYTHON_SHA256)"
    $baseReady = Test-ManagedPythonBase $basePython $baseStamp $expectedBase $config.WINDOWS_PYTHON_VERSION
    if (-not $baseReady) {
        if ($VerifyOnly) { Fail "Managed Python base is missing or mismatched at $baseRoot (verify-only)." }
        Install-ManagedPythonBase $config $baseRoot $expectedBase
    }

    $requirementsHash = (Get-FileHash -LiteralPath $Requirements -Algorithm SHA256).Hash.ToLowerInvariant()
    $venvStamp = Join-Path $VenvRoot '.jaszczurhal-component-version'
    $expectedVenv = "python|$($config.WINDOWS_PYTHON_VERSION)|requirements|$requirementsHash"
    if (-not (Test-ManagedVenv $VenvPython $venvStamp $expectedVenv)) {
        if ($VerifyOnly) { Fail "Managed Python environment is missing or mismatched at $VenvRoot (verify-only)." }
        if (Test-Path -LiteralPath $VenvRoot) { Remove-Item -LiteralPath $VenvRoot -Recurse -Force }
        New-Item -ItemType Directory -Path (Split-Path -Parent $VenvRoot) -Force | Out-Null
        Invoke-Checked $basePython @('-m', 'venv', $VenvRoot) 'Creating managed Python environment'
        Invoke-Checked $VenvPython @(
            '-m', 'pip', 'install', '--disable-pip-version-check',
            '--require-hashes', '-r', $Requirements
        ) 'Installing pinned Python dependencies'
        Set-Content -LiteralPath $venvStamp -Value $expectedVenv -Encoding ASCII
    }
    Invoke-Checked $VenvPython @(
        '-c', 'import serial,sys; print(sys.version.split()[0], serial.__version__)'
    ) 'Verifying managed Python environment'
}

function Add-ResolvedToolsToPath {
    $state = Join-Path $ToolsRoot 'resolved-tools.json'
    if (-not (Test-Path -LiteralPath $state)) { Fail "Resolved tool state missing: $state" }
    $resolved = Get-Content -LiteralPath $state -Raw | ConvertFrom-Json
    $directories = New-Object System.Collections.Generic.List[string]
    foreach ($property in $resolved.PSObject.Properties) {
        $directories.Add((Split-Path -Parent $property.Value))
    }
    $directories.Add((Split-Path -Parent $VenvPython))
    $env:Path = (($directories | Select-Object -Unique) -join ';') + ';' + $env:Path
}

function Sync-HostEnvironment {
    $resolvedPath = Join-Path $ToolsRoot 'resolved-tools.json'
    if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
        Fail "Resolved tool state missing: $resolvedPath"
    }
    $resolved = Get-Content -LiteralPath $resolvedPath -Raw | ConvertFrom-Json
    $state = [ordered]@{
        schemaVersion = 1
        toolsRoot = $ToolsRoot
        buildRoot = $BuildRoot
        python = $VenvPython
        tools = $resolved
    }
    $serialized = ($state | ConvertTo-Json -Depth 4) + "`n"
    if ($VerifyOnly) {
        if (-not (Test-Path -LiteralPath $HostEnvironment -PathType Leaf)) {
            Fail "Windows host environment state is missing: $HostEnvironment"
        }
        $current = Get-Content -LiteralPath $HostEnvironment -Raw
        if ($current -ne $serialized) {
            Fail "Windows host environment state is stale: $HostEnvironment"
        }
        return
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $HostEnvironment) `
        -Force | Out-Null
    if (-not (Test-Path -LiteralPath $HostEnvironment -PathType Leaf) -or
        (Get-Content -LiteralPath $HostEnvironment -Raw) -ne $serialized) {
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        [IO.File]::WriteAllText($HostEnvironment, $serialized, $utf8NoBom)
    }
}

if ($VerifyOnly -and ($ConfigureHost -or $InstallExtensions)) {
    Fail '-VerifyOnly cannot be combined with -ConfigureHost or -InstallExtensions.'
}
if ($env:OS -ne 'Windows_NT') { Fail 'runmefirst.ps1 requires native Windows.' }
if ($env:PROCESSOR_ARCHITECTURE -ne 'AMD64') { Fail 'Only native AMD64 Windows is supported.' }

$ToolsRoot = [IO.Path]::GetFullPath($ToolsRoot)
$BuildRoot = [IO.Path]::GetFullPath($BuildRoot)
if ($ToolsRoot.Length -gt 80 -or $BuildRoot.Length -gt 80) {
    Fail 'ToolsRoot and BuildRoot must each stay at or below 80 characters. Use short physical paths.'
}

Write-Host ''
Write-Host 'JaszczurHAL native Windows setup plan' -ForegroundColor Cyan
Write-Host "  mode:       $(if ($VerifyOnly) { 'verify only' } else { 'install/repair' })"
Write-Host "  repository: $RepoRoot"
Write-Host "  tools root: $ToolsRoot"
Write-Host "  build root: $BuildRoot"
Write-Host "  Python env: $VenvRoot"
Write-Host '  components: managed Python 3.12 + pyserial, source checkouts, CMake, Ninja,'
Write-Host '              GNU Arm, GNU RISC-V, OpenOCD and picotool'
Write-Host "  host changes allowed: $([bool]$ConfigureHost)"
Write-Host "  extension installation allowed: $([bool]$InstallExtensions)"
Write-Host "  firmware-only inventory: $([bool]$FirmwareOnly)"
Write-Host '  elevation: this script never elevates itself'
Write-Host ''

if ($RepoRoot.StartsWith('\\')) {
    Fail 'Native Windows setup requires a checkout on a local Windows volume. WSL UNC paths are not supported.'
}

Prepare-HostSettings
Ensure-ManagedPython

$componentArguments = @('source-components', '--repo-root', $RepoRoot)
if ($VerifyOnly) { $componentArguments += '--verify-only' } else { $componentArguments += '--force' }
Invoke-Checked $VenvPython (@($ComponentManager) + $componentArguments) 'Synchronizing source components'

$toolArguments = @(
    $ComponentManager, 'windows-tools', '--repo-root', $RepoRoot,
    '--tools-root', $ToolsRoot, '--build-root', $BuildRoot
)
if ($VerifyOnly) { $toolArguments += '--verify-only' }
if ($Force) { $toolArguments += '--prefer-managed' }
Invoke-Checked $VenvPython $toolArguments 'Preparing native Windows tools'

Add-ResolvedToolsToPath
Sync-HostEnvironment
if ($InstallExtensions) {
    Invoke-Checked $VenvPython @(
        (Join-Path $RepoRoot 'vscode\tools\manage_vscode_extensions.py'),
        '--install', '--yes'
    ) 'Installing VS Code extensions'
}

$inventoryArguments = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $Inventory,
    '-RepoPath', $RepoRoot
)
if ($FirmwareOnly) { $inventoryArguments += '-FirmwareOnly' }
Invoke-Checked 'powershell.exe' $inventoryArguments 'Windows host contract self-check'

Write-Host ''
Write-Host 'Resolved managed tools:' -ForegroundColor Cyan
Get-Content -LiteralPath (Join-Path $ToolsRoot 'resolved-tools.json') | Write-Host
Write-Host "Managed Python: $VenvPython"
Write-Host "Managed build root: $BuildRoot"
Write-Host 'JaszczurHAL native Windows setup is ready.' -ForegroundColor Green
