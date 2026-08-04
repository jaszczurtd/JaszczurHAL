<#
.SYNOPSIS
    JaszczurHAL Windows host inventory and contract check.

.DESCRIPTION
    Read-only probe of a Windows host against the JaszczurHAL native firmware
    workflow contract. Reports every item as ok / warn / fail / absent and exits
    non-zero when a required item is not satisfied.

    Requires no administrator rights and changes nothing on the host. Written
    for Windows PowerShell 5.1 so it runs on a stock Windows 10 install.

    Later this becomes the basis of the bootstrap --verify-only self-check.

.PARAMETER Json
    Emit the raw result objects as JSON instead of the text report.

.PARAMETER RepoPath
    Optional JaszczurHAL checkout. Adds repo-side checks for .gitattributes and
    launcher line endings.

.PARAMETER FirmwareOnly
    Verify the native firmware build contract without requiring VS Code or its
    extensions. Editor checks remain visible as optional inventory entries.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\windows_host_inventory.ps1

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\windows_host_inventory.ps1 -Json > host.json
#>

[CmdletBinding()]
param(
    [switch]$Json,
    [switch]$FirmwareOnly,
    [string]$RepoPath
)

$ErrorActionPreference = 'Continue'

# Contract floors. The firmware dispatcher requires CMake 3.20; the pinned Pico
# SDK 2.2.0 declares 3.13...3.27. Windows 10 1809 is build 17763.
$MIN_OS_BUILD    = 17763
$MIN_CMAKE       = [version]'3.20'
$UNTESTED_CMAKE  = [version]'4.0'
$MIN_PYTHON      = [version]'3.9'
$TESTED_PYTHON    = [version]'3.11'
$TESTED_PYTHON_MAX = [version]'3.13'
# Hard requirements, each traceable to a checked-in VS Code file:
#   cpptools      - IntelliSense over compile_commands_patched.json
#   cortex-debug  - launch.json declares "type": "cortex-debug"
$REQUIRED_VSCODE = @(
    'ms-vscode.cpptools',
    'marus25.cortex-debug'
)

# Referenced but not load-bearing for build/upload/monitor.
$OPTIONAL_VSCODE = @{
    'ms-vscode.cmake-tools'          = 'Named as C_Cpp.default.configurationProvider in settings.json. Without it cpptools falls back to compileCommands.'
    'ms-vscode.vscode-serial-monitor' = 'Convenience only; tasks use the jh-vscode monitor actions.'
}

$results = New-Object System.Collections.ArrayList

function Add-Result {
    param(
        [string]$Name,
        [ValidateSet('required','optional','info')][string]$Category,
        [ValidateSet('ok','warn','fail','absent')][string]$Status,
        [string]$Found,
        [string]$Expected,
        [string]$Note
    )
    $null = $results.Add([pscustomobject]@{
        Name     = $Name
        Category = $Category
        Status   = $Status
        Found    = $Found
        Expected = $Expected
        Note     = $Note
    })
}

# Run a native tool, swallow its error stream, return trimmed stdout+stderr.
function Invoke-Tool {
    param([string]$Exe, [string[]]$Arguments = @())
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    try {
        $out = & $Exe @Arguments 2>&1 | Out-String
        if ($null -eq $out) { return '' }
        return $out.Trim()
    } catch {
        return ''
    } finally {
        $ErrorActionPreference = $prev
    }
}

function Get-ToolPath {
    param([string]$Name)
    $cmd = Get-Command -Name $Name -CommandType Application -ErrorAction SilentlyContinue |
           Select-Object -First 1
    if ($cmd) { return $cmd.Source }
    return $null
}

function Get-FirstVersion {
    param([string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) { return $null }
    $m = [regex]::Match($Text, '(\d+)\.(\d+)(?:\.(\d+))?')
    if (-not $m.Success) { return $null }
    $patch = 0
    if ($m.Groups[3].Success) { $patch = [int]$m.Groups[3].Value }
    return [version]::new([int]$m.Groups[1].Value, [int]$m.Groups[2].Value, $patch)
}

# ── Operating system ────────────────────────────────────────────────────────
$os    = Get-CimInstance Win32_OperatingSystem -ErrorAction SilentlyContinue
$build = 0
if ($os) { [void][int]::TryParse($os.BuildNumber, [ref]$build) }

$display = ''
try {
    $cv = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion' -ErrorAction Stop
    if ($cv.DisplayVersion) { $display = $cv.DisplayVersion } elseif ($cv.ReleaseId) { $display = $cv.ReleaseId }
    if ($build -eq 0 -and $cv.CurrentBuildNumber) {
        [void][int]::TryParse("$($cv.CurrentBuildNumber)", [ref]$build)
    }
} catch { }

$caption = 'Windows'
if ($os -and $os.Caption) { $caption = $os.Caption }
$osFound = "$caption build $build"
if ($display) { $osFound = "$osFound ($display)" }
Add-Result 'Windows version' 'required' $(if ($build -ge $MIN_OS_BUILD) { 'ok' } else { 'fail' }) `
    $osFound "build >= $MIN_OS_BUILD (Windows 10 1809)" ''

$arch = $env:PROCESSOR_ARCHITECTURE
Add-Result 'Architecture' 'required' $(if ($arch -eq 'AMD64') { 'ok' } else { 'fail' }) `
    $arch 'AMD64' 'Pinned toolchain assets are x64-win only.'

# ── Long path support ───────────────────────────────────────────────────────
$longPaths = $null
try {
    $longPaths = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\FileSystem' `
                    -Name LongPathsEnabled -ErrorAction Stop).LongPathsEnabled
} catch { }
$lpStatus = 'fail'
if ($longPaths -eq 1) { $lpStatus = 'ok' }
Add-Result 'LongPathsEnabled' 'required' $lpStatus `
    $(if ($null -eq $longPaths) { 'not set' } else { "$longPaths" }) '1' `
    'Pico SDK plus Ninja object dirs exceed MAX_PATH. Needs an admin registry change.'

# ── Git ─────────────────────────────────────────────────────────────────────
$repoAttributesPresent = $false
if ($RepoPath -and (Test-Path (Join-Path $RepoPath '.gitattributes'))) {
    $repoAttributesPresent = $true
}
$gitPath = Get-ToolPath 'git'
if ($gitPath) {
    $gitVer = Get-FirstVersion (Invoke-Tool 'git' @('--version'))
    Add-Result 'Git' 'required' 'ok' "$gitVer ($gitPath)" 'any recent Git for Windows' ''

    $autocrlf = Invoke-Tool 'git' @('config','--get','core.autocrlf')
    $crlfRisk = $true
    if ($autocrlf -eq 'false' -or $autocrlf -eq 'input') { $crlfRisk = $false }
    if ($repoAttributesPresent) { $crlfRisk = $false }
    if ([string]::IsNullOrWhiteSpace($autocrlf)) { $autocrlf = 'unset (Git for Windows installs true)' }
    Add-Result 'git core.autocrlf' 'info' $(if ($crlfRisk) { 'warn' } else { 'ok' }) $autocrlf 'any' `
        $(if ($repoAttributesPresent) { 'Repository .gitattributes protects platform-specific line endings.' } else { 'Without .gitattributes, autocrlf=true can break Unix launchers and hooks.' })

    $lp = Invoke-Tool 'git' @('config','--get','core.longpaths')
    $lpOk = ($lp -eq 'true')
    Add-Result 'git core.longpaths' 'required' $(if ($lpOk) { 'ok' } else { 'fail' }) `
        $(if ([string]::IsNullOrWhiteSpace($lp)) { 'unset' } else { $lp }) 'true' `
        'Set with: git config --global core.longpaths true'
} else {
    Add-Result 'Git' 'required' 'absent' 'not found' 'Git for Windows' ''
    Add-Result 'git core.longpaths' 'required' 'absent' 'n/a' 'true' 'Git missing.'
}

# ── Python interpreters ─────────────────────────────────────────────────────
# The runtime needs an interpreter that also has pyserial. Presence of an
# interpreter alone does not satisfy the contract.
$candidates = @(
    [pscustomobject]@{ Label = 'py -3';  Exe = 'py';     Pre = @('-3') },
    [pscustomobject]@{ Label = 'python'; Exe = 'python'; Pre = @() }
)

$satisfying = @()
foreach ($c in $candidates) {
    if (-not (Get-ToolPath $c.Exe)) {
        Add-Result "Python ($($c.Label))" 'info' 'absent' 'not found' '-' ''
        continue
    }
    $verArgs = @($c.Pre) + @('-c', "import sys;print('.'.join(map(str,sys.version_info[:3])))")
    $verOut  = Invoke-Tool $c.Exe $verArgs
    $ver     = Get-FirstVersion $verOut
    if (-not $ver) {
        Add-Result "Python ($($c.Label))" 'info' 'warn' 'version unreadable' "$MIN_PYTHON+" $verOut
        continue
    }
    $serArgs = @($c.Pre) + @('-c', 'import serial;print(serial.__version__)')
    $serOut  = Invoke-Tool $c.Exe $serArgs
    $serVer  = $null
    if ($serOut -notmatch 'ModuleNotFoundError|Traceback') { $serVer = Get-FirstVersion $serOut }

    $note = ''
    $outside = $false
    if ($ver -lt $TESTED_PYTHON) {
        $note = "Below the tested floor $TESTED_PYTHON; Linux CI runs 3.12."
        $outside = $true
    } elseif ($ver.Major -gt $TESTED_PYTHON_MAX.Major -or
              ($ver.Major -eq $TESTED_PYTHON_MAX.Major -and $ver.Minor -gt $TESTED_PYTHON_MAX.Minor)) {
        $note = "Above the tested range (<= $TESTED_PYTHON_MAX); Linux CI runs 3.12 and the runtime has not been validated here."
        $outside = $true
    }
    if ($serVer) {
        $found = "$ver, pyserial $serVer"
        if ($ver -ge $MIN_PYTHON) { $satisfying += "$($c.Label) ($ver)" }
        Add-Result "Python ($($c.Label))" 'info' $(if ($outside) { 'warn' } else { 'ok' }) `
            $found "$MIN_PYTHON+ with pyserial, tested $TESTED_PYTHON-$TESTED_PYTHON_MAX" $note
    } else {
        Add-Result "Python ($($c.Label))" 'info' 'warn' "$ver, no pyserial" "$MIN_PYTHON+ with pyserial" `
            'Interpreter present but unusable for upload/monitor without pyserial.'
    }
}

Add-Result 'Python satisfying contract' 'required' $(if ($satisfying.Count -gt 0) { 'ok' } else { 'fail' }) `
    $(if ($satisfying.Count -gt 0) { $satisfying -join '; ' } else { 'none' }) `
    "at least one interpreter >= $MIN_PYTHON with pyserial" `
    'A managed venv with a pinned pyserial is the intended first choice.'

# Microsoft Store alias trap: a 0-byte python3.exe under WindowsApps.
$stub = Join-Path $env:LOCALAPPDATA 'Microsoft\WindowsApps\python3.exe'
if (Test-Path $stub) {
    $len = (Get-Item $stub).Length
    if ($len -eq 0) {
        Add-Result 'python3 Store alias' 'info' 'warn' '0-byte stub present' 'excluded from detection' `
            'python3.exe under WindowsApps is a Store alias; never use it for interpreter detection.'
    }
}

# ── Build tools ─────────────────────────────────────────────────────────────
$cmakePath = Get-ToolPath 'cmake'
if ($cmakePath) {
    $cmVer = Get-FirstVersion (Invoke-Tool 'cmake' @('--version'))
    $st = 'ok'; $note = ''
    if ($cmVer -lt $MIN_CMAKE) { $st = 'fail' }
    elseif ($cmVer -ge $UNTESTED_CMAKE) {
        $st = 'warn'
        $note = "CMake 4.x is untested against the pinned Pico SDK 2.2.0, which declares 3.13...3.27."
    }
    Add-Result 'CMake' 'required' $st "$cmVer ($cmakePath)" ">= $MIN_CMAKE" $note
} else {
    Add-Result 'CMake' 'required' 'absent' 'not found' ">= $MIN_CMAKE" ''
}

$ninjaPath = Get-ToolPath 'ninja'
if ($ninjaPath) {
    $nVer = Get-FirstVersion (Invoke-Tool 'ninja' @('--version'))
    Add-Result 'Ninja' 'required' 'ok' "$nVer ($ninjaPath)" 'any' 'Chosen firmware generator on Windows.'
} else {
    Add-Result 'Ninja' 'required' 'absent' 'not found' 'any' 'Chosen firmware generator on Windows.'
}

$armPath = Get-ToolPath 'arm-none-eabi-gcc'
if ($armPath) {
    $aVer = Get-FirstVersion (Invoke-Tool 'arm-none-eabi-gcc' @('-dumpversion'))
    Add-Result 'GNU Arm Embedded' 'required' 'ok' "$aVer ($armPath)" 'any' 'Linux gate uses 13.2.1.'
} else {
    Add-Result 'GNU Arm Embedded' 'required' 'absent' 'not found' 'arm-none-eabi-gcc' `
        'Needed for rp2040, rp2350-arm and stm32g474.'
}

$rvPath = Get-ToolPath 'riscv32-unknown-elf-gcc'
if ($rvPath) {
    $rVer = Get-FirstVersion (Invoke-Tool 'riscv32-unknown-elf-gcc' @('-dumpversion'))
    Add-Result 'GNU RISC-V' 'optional' 'ok' "$rVer ($rvPath)" 'gcc 15' 'Only needed for rp2350-riscv.'
} else {
    Add-Result 'GNU RISC-V' 'optional' 'absent' 'not found' 'riscv32-unknown-elf-gcc' `
        'Managed asset: riscv-toolchain-15-x64-win.zip from pico-sdk-tools v2.2.0-4.'
}

foreach ($t in @(
    @{ N='picotool'; C='optional'; Note='Managed asset: picotool-2.2.0-a4-x64-win.zip. Plain COM/BOOTSEL upload does not need it.' },
    @{ N='openocd';  C='optional'; Note='Managed asset: openocd-0.12.0+dev-x64-win.zip. Debug and probe paths only.' }
)) {
    $p = Get-ToolPath $t.N
    if ($p) { Add-Result $t.N $t.C 'ok' $p 'managed install preferred' $t.Note }
    else    { Add-Result $t.N $t.C 'absent' 'not found' 'managed install' $t.Note }
}

# Convenience installers. Neither is required; pinned managed downloads are.
foreach ($t in @(
    @{ N='winget'; Note='Not required. Absent on Windows 10 LTSC images, which ship without the Store and App Installer.' },
    @{ N='choco';  Note='Not required. Alternative convenience installer, version-independent.' }
)) {
    $p = Get-ToolPath $t.N
    if ($p) { Add-Result "$($t.N) (convenience)" 'info' 'ok' (Invoke-Tool $t.N @('--version')) '-' '' }
    else    { Add-Result "$($t.N) (convenience)" 'info' 'absent' 'not found' '-' $t.Note }
}

# ── VS Code ─────────────────────────────────────────────────────────────────
$editorCategory = $(if ($FirmwareOnly) { 'optional' } else { 'required' })
$codePath = Get-ToolPath 'code'
if (-not $codePath) { $codePath = Get-ToolPath 'code.cmd' }
if ($codePath) {
    $cv = Invoke-Tool $codePath @('--version')
    $cvFirst = ($cv -split "`n")[0]
    Add-Result 'VS Code' $editorCategory 'ok' $cvFirst 'any recent' ''

    $extText = Invoke-Tool $codePath @('--list-extensions')
    $ext = @()
    if ($extText) { $ext = $extText -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ } }

    $missing = @($REQUIRED_VSCODE | Where-Object { $ext -notcontains $_ })
    if ($missing.Count -eq 0) {
        Add-Result 'VS Code extensions' $editorCategory 'ok' "$($ext.Count) installed" ($REQUIRED_VSCODE -join ', ') ''
    } else {
        Add-Result 'VS Code extensions' $editorCategory 'fail' "missing: $($missing -join ', ')" `
            ($REQUIRED_VSCODE -join ', ') `
            ("Install with: code --install-extension " + ($missing -join ' --install-extension '))
    }

    foreach ($id in $OPTIONAL_VSCODE.Keys) {
        if ($ext -contains $id) {
            Add-Result "ext $id" 'optional' 'ok' 'installed' '-' ''
        } else {
            Add-Result "ext $id" 'optional' 'absent' 'not installed' '-' $OPTIONAL_VSCODE[$id]
        }
    }

    # Drift between the host and what the repo declares in extensions.json.
    if ($RepoPath -and (Test-Path $RepoPath)) {
        $decl = Join-Path $RepoPath 'vscode\examples\extensions.json'
        if (Test-Path $decl) {
            try {
                $raw = (Get-Content $decl -Raw) -replace '(?m)^\s*//.*$', ''
                $rec = @(($raw | ConvertFrom-Json).recommendations)
                $recMissing = @($rec | Where-Object { $ext -notcontains $_ })
                Add-Result 'Declared recommendations' 'info' $(if ($recMissing.Count -eq 0) { 'ok' } else { 'warn' }) `
                    $(if ($recMissing.Count -eq 0) { "$($rec.Count) declared, all installed" } else { "not installed: $($recMissing -join ', ')" }) `
                    'vscode/examples/extensions.json' ''
            } catch {
                Add-Result 'Declared recommendations' 'info' 'warn' 'extensions.json unparseable' `
                    'vscode/examples/extensions.json' ''
            }
        } else {
            Add-Result 'Declared recommendations' 'info' 'warn' 'vscode/examples/extensions.json absent' `
                'template should declare recommendations' `
                'Consumers ship .vscode/extensions.json, the JaszczurHAL template does not, so generated examples and fixtures get none.'
        }
    }
} else {
    Add-Result 'VS Code' $editorCategory 'absent' 'not found' 'any recent' ''
    Add-Result 'VS Code extensions' $editorCategory 'absent' 'n/a' ($REQUIRED_VSCODE -join ', ') 'VS Code missing.'
}

# ── Anti-malware ────────────────────────────────────────────────────────────
# Defender being off does not mean nothing scans the build tree. Enumerate the
# registered products first, then read Defender state separately: on a host
# where Defender stood down, Get-MpPreference fails while Get-MpComputerStatus
# still answers.
$avProducts = @()
try {
    $avProducts = @(Get-CimInstance -Namespace root/SecurityCenter2 -ClassName AntiVirusProduct -ErrorAction Stop |
                    Select-Object -ExpandProperty displayName)
} catch { }

if ($avProducts.Count -gt 0) {
    $thirdParty = @($avProducts | Where-Object { $_ -notmatch 'Windows Defender|Microsoft Defender' })
    if ($thirdParty.Count -gt 0) {
        Add-Result 'Anti-malware products' 'info' 'warn' ($avProducts -join ', ') '-' `
            'A third-party scanner is active. It can slow builds and quarantine fresh .exe or UF2 output, and its exclusions may be centrally managed and not user-changeable.'
    } else {
        Add-Result 'Anti-malware products' 'info' 'ok' ($avProducts -join ', ') '-' ''
    }
} else {
    Add-Result 'Anti-malware products' 'info' 'warn' 'unreadable' '-' `
        'SecurityCenter2 not readable; the active scanner is unknown.'
}

$rtState = 'unreadable'
try {
    $st = Get-MpComputerStatus -ErrorAction Stop
    $rtState = "real-time=$($st.RealTimeProtectionEnabled), service=$($st.AMServiceEnabled)"
} catch { }

$excState = 'unreadable (Defender service not running or access denied)'
$excKnown = $false
try {
    $pr = Get-MpPreference -ErrorAction Stop
    if ($pr.ExclusionPath) {
        # Without elevation this property can answer with an N/A notice
        # instead of the real list.
        $joined = ($pr.ExclusionPath -join '; ')
        if ($joined -match 'must be an administrator|^N/A') {
            $excState = "unreadable without elevation ($joined)"
        } else {
            $excState = $joined
            $excKnown = $true
        }
    } else {
        $excState = 'none configured'
        $excKnown = $true
    }
} catch { }

Add-Result 'Defender state' 'info' 'ok' $rtState '-' ''
Add-Result 'Defender exclusions' 'info' $(if ($excKnown) { 'ok' } else { 'warn' }) `
    $excState '-' 'An unreadable value is not the same as no exclusions.'

# ── Devices ─────────────────────────────────────────────────────────────────
$usbipd = Get-ToolPath 'usbipd'
if ($usbipd) {
    $attached = Invoke-Tool $usbipd @('list')
    $hasAttached = $attached -match 'Attached'
    Add-Result 'usbipd-win' 'info' $(if ($hasAttached) { 'warn' } else { 'ok' }) `
        $(if ($hasAttached) { 'installed, device(s) attached to WSL' } else { 'installed, nothing attached' }) '-' `
        'A device attached to WSL exposes neither a COM port nor a BOOTSEL drive letter to Windows.'
} else {
    Add-Result 'usbipd-win' 'info' 'absent' 'not found' '-' 'Only relevant when sharing devices with WSL.'
}

$ports = @()
try { $ports = [System.IO.Ports.SerialPort]::GetPortNames() } catch { }
Add-Result 'COM ports' 'info' 'ok' `
    $(if ($ports.Count -gt 0) { $ports -join ', ' } else { 'none present' }) '-' `
    'Hardware smoke for stages 6-8 needs the device visible to Windows.'

$sys = ($env:SystemDrive)
try {
    $ld = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='$sys'" -ErrorAction Stop
    $freeGb = [math]::Round($ld.FreeSpace / 1GB, 1)
    Add-Result 'Free space (system drive)' 'info' $(if ($freeGb -ge 10) { 'ok' } else { 'warn' }) `
        "$freeGb GB on $sys" '>= 10 GB recommended' `
        'RISC-V toolchain alone is a 131 MB archive; SDK, toolchains and build trees add up.'
} catch { }

# ── Optional repo-side checks ───────────────────────────────────────────────
if ($RepoPath) {
    if (Test-Path $RepoPath) {
        $ga = Join-Path $RepoPath '.gitattributes'
        Add-Result '.gitattributes' 'info' $(if (Test-Path $ga) { 'ok' } else { 'warn' }) `
            $(if (Test-Path $ga) { 'present' } else { 'absent' }) 'present' `
            'Without it, core.autocrlf rewrites *.sh and hooks to CRLF.'

        $entry = Join-Path $RepoPath 'vscode\entry\jh-vscode'
        if (Test-Path $entry) {
            $bytes = [System.IO.File]::ReadAllBytes($entry)
            $crlf = $false
            for ($i = 1; $i -lt $bytes.Length; $i++) {
                if ($bytes[$i] -eq 10 -and $bytes[$i-1] -eq 13) { $crlf = $true; break }
            }
            Add-Result 'jh-vscode line endings' 'info' $(if ($crlf) { 'warn' } else { 'ok' }) `
                $(if ($crlf) { 'CRLF' } else { 'LF' }) 'LF' `
                'A CRLF shebang breaks the Unix launcher.'
        }
    } else {
        Add-Result 'RepoPath' 'info' 'warn' "not found: $RepoPath" 'existing checkout' ''
    }
}

# ── Output ──────────────────────────────────────────────────────────────────
$failed = @($results | Where-Object { $_.Category -eq 'required' -and $_.Status -ne 'ok' })

if ($Json) {
    $results | ConvertTo-Json -Depth 4
} else {
    $glyph = @{ ok = '[ ok ]'; warn = '[warn]'; fail = '[FAIL]'; absent = '[ -- ]' }

    Write-Host ''
    Write-Host 'JaszczurHAL Windows host inventory' -ForegroundColor Cyan
    Write-Host ("host: {0}   user: {1}   date: {2}" -f $env:COMPUTERNAME, $env:USERNAME, (Get-Date -Format 'yyyy-MM-dd HH:mm'))
    Write-Host ''

    foreach ($group in @('required','optional','info')) {
        $rows = @($results | Where-Object { $_.Category -eq $group })
        if ($rows.Count -eq 0) { continue }
        Write-Host ("== {0} ==" -f $group.ToUpper())
        foreach ($r in $rows) {
            $color = 'Gray'
            switch ($r.Status) {
                'ok'     { $color = 'Green' }
                'warn'   { $color = 'Yellow' }
                'fail'   { $color = 'Red' }
                'absent' { if ($group -eq 'required') { $color = 'Red' } else { $color = 'DarkGray' } }
            }
            Write-Host ("  {0} {1,-28} {2}" -f $glyph[$r.Status], $r.Name, $r.Found) -ForegroundColor $color
            if ($r.Expected -and $r.Expected -ne '-' -and $r.Status -ne 'ok') {
                Write-Host ("         expected: {0}" -f $r.Expected) -ForegroundColor DarkGray
            }
            if ($r.Note -and $r.Status -ne 'ok') {
                Write-Host ("         {0}" -f $r.Note) -ForegroundColor DarkGray
            }
        }
        Write-Host ''
    }

    if ($failed.Count -eq 0) {
        Write-Host 'Contract satisfied: all required items ok.' -ForegroundColor Green
    } else {
        Write-Host ("Contract NOT satisfied: {0} required item(s) outstanding." -f $failed.Count) -ForegroundColor Red
        foreach ($f in $failed) { Write-Host ("  - {0}: {1}" -f $f.Name, $f.Found) -ForegroundColor Red }
    }
    Write-Host ''
    Write-Host 'Nothing on this host was modified.' -ForegroundColor DarkGray
    Write-Host ''
}

if ($failed.Count -gt 0) { exit 1 }
exit 0
