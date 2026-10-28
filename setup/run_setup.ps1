# Runs openport_winusb.ps1, NC Flash's Tactrix OpenPort driver switch, without
# NC Flash: one UAC prompt, then prints the switch's log. Started by
# "Install WinUSB driver.cmd" and "Restore Tactrix driver.cmd".
#
# The switch is the same script NC Flash (2.24 beta or later) runs, so either
# one can undo the other, and they never run at the same time (the script
# holds a system-wide lock). The release zip ships it unchanged from NC Flash's
# repository.
#
# The elevated PowerShell re-reads openport_winusb.ps1 and runs it only if its
# SHA-256 still matches the one taken here before the UAC prompt, so the file
# can't be swapped in between.
#
# Exit codes: the script's (0 done, 1 failed, 3 done but restart Windows, 5
# another switch is running), 2 UAC prompt declined, 4 Windows couldn't start
# it, 6 openport_winusb.ps1 changed before it ran, 7 refused before asking for
# administrator rights (nothing changed).

param([Parameter(Mandatory = $true)][ValidateSet('Install', 'Restore')][string]$Action)

$ErrorActionPreference = 'Stop'
$mode = $Action
$scriptPath = Join-Path $PSScriptRoot 'openport_winusb.ps1'
$powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$logName = 'standalone_' + [guid]::NewGuid().ToString('N') + '.log'
$logPath = Join-Path $env:ProgramData "NC Flash\OpenPort\$logName"

function Stop-Setup([string]$Message) {
    Write-Host $Message
    Write-Host 'Nothing was changed.'
    exit 7
}

if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    Write-Host "openport_winusb.ps1 is missing. Extract the whole zip, then run this again."
    exit 1
}

# Switching the driver unplugs the cable from Windows' point of view: doing it
# during a read or a flash would cut it short.
$busy = @(Get-Process -Name 'romdrop*', 'ecuflash*', 'NCFlash', 'j2534_bridge_32' -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty ProcessName -Unique)
if ($busy) {
    Stop-Setup "Close $($busy -join ', ') first: changing the cable's driver during a read or a flash would cut it short."
}

function Read-CableState {
    # The switch's own Get-CableState. Loaded in a child scope: loading the
    # switch also binds its own -Action/-LogName, which must not overwrite
    # this script's variables (PowerShell names ignore case).
    & { . $scriptPath; Get-CableState }
}

$before = Read-CableState
Write-Host "Cable now: $before"
if ($mode -eq 'Install') {
    if ($before -eq 'not plugged in') { Stop-Setup 'Plug the cable in (it can keep its SD card), then run this again.' }
    if ($before -like 'WinUSB *') { Write-Host 'The cable is already on WinUSB; setting it up again.' }
    if ($before -like 'openport (problem 0)') {
        Write-Host ''
        Write-Host "Tactrix's own driver works on this PC. After the switch, programs that use it"
        Write-Host '(EcuFlash, RomDrop without this driver, NC Flash 2.23 and earlier) cannot use'
        Write-Host 'the cable until you run "Restore Tactrix driver.cmd".'
        if ((Read-Host 'Type Y to switch anyway') -notmatch '^\s*[Yy]') { Stop-Setup 'Cancelled.' }
    }
}

$sha256 = [Security.Cryptography.SHA256]::Create()
$hash = [BitConverter]::ToString($sha256.ComputeHash([IO.File]::ReadAllBytes($scriptPath))) -replace '-', ''

# EscapeSingleQuotedStringContent also doubles the curly quotes PowerShell
# treats as single quotes (e.g. a folder named "Charles’s files").
$quotedPath = [Management.Automation.Language.CodeGeneration]::EscapeSingleQuotedStringContent($scriptPath)
$inner = @"
`$b = [IO.File]::ReadAllBytes('$quotedPath')
`$h = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash(`$b)) -replace '-', ''
if (`$h -ne '$hash') { exit 6 }
& ([scriptblock]::Create([Text.Encoding]::UTF8.GetString(`$b).TrimStart([char]0xFEFF))) -Action '$mode' -LogName '$logName'
exit 1
"@
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($inner))

if ($mode -eq 'Install') {
    Write-Host 'Putting the Tactrix OpenPort 2.0 on WinUSB. Keep the cable plugged in.'
} else {
    Write-Host "Putting the Tactrix OpenPort 2.0 back on Tactrix's driver."
}
Write-Host 'Windows will ask for administrator rights.'
Write-Host ''

# Process.Start rather than Start-Process: in Windows PowerShell 5.1 the latter
# hides the Windows error, so a declined UAC prompt looked like a launch failure.
$psi = New-Object Diagnostics.ProcessStartInfo $powershell
$psi.Arguments = "-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand $encoded"
$psi.Verb = 'runas'
$psi.UseShellExecute = $true
$psi.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
try {
    $p = [Diagnostics.Process]::Start($psi)
    if ($p) { $p.WaitForExit(); $code = $p.ExitCode } else { $code = 4 }
} catch {
    $e = $_.Exception
    while ($e -and -not ($e -is [ComponentModel.Win32Exception])) { $e = $e.InnerException }
    # 1223 = ERROR_CANCELLED: the user said No to the UAC prompt.
    if ($e -and $e.NativeErrorCode -eq 1223) { $code = 2 } else { $code = 4 }
}

$hasLog = Test-Path -LiteralPath $logPath
if ($hasLog) {
    Get-Content -LiteralPath $logPath | ForEach-Object { Write-Host "  $_" }
    Write-Host ''
}
$after = Read-CableState
switch ($code) {
    { $_ -in 0, 3 } {
        $restart = if ($code -eq 3) { ' Restart Windows to finish.' } else { '' }
        if ($mode -eq 'Install') {
            if ($after -like 'WinUSB (problem 0)') {
                Write-Host "Done: the cable is on WinUSB.$restart Put j2534.dll and libusb-1.0.dll next to your program (see the README)."
            } else {
                Write-Host "The setup finished, but the cable is not working on WinUSB yet (now: $after).$restart"
            }
        } elseif ($after -like 'openport *') {
            Write-Host "Done: the cable is back on Tactrix's driver ($after).$restart Windows 11 may block that driver (problem 39 = Code 39 in Device Manager)."
        } elseif ($after -eq 'not plugged in') {
            Write-Host "Done: the WinUSB setup is removed.$restart The cable will get Tactrix's driver (if installed) when you plug it in."
        } else {
            Write-Host "The WinUSB setup is removed, but the cable is now: $after.$restart If Tactrix's software isn't installed, the cable has no driver; that is expected."
        }
    }
    2 { Write-Host 'Cancelled: nothing was changed.' }
    4 { Write-Host 'Windows could not start the setup. Nothing was changed.' }
    5 { Write-Host 'Another driver switch is running (NC Flash?). Wait for it to finish, then try again.' }
    6 { Write-Host 'openport_winusb.ps1 changed while waiting for the prompt, so nothing was run. Extract the zip again.' }
    default {
        if ($hasLog) {
            Write-Host "Failed (exit code $code). The log above says why; log file: $logPath"
        } else {
            Write-Host "Failed (exit code $code) before the switch started, so nothing was changed. If the zip is on a network drive, copy it to a local folder and try again."
        }
    }
}
exit $code
