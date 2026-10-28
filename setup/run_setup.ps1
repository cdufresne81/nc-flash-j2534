# Runs openport_winusb.ps1, NC Flash's Tactrix OpenPort driver switch, without
# NC Flash: one UAC prompt, then prints the switch's log. Started by
# "Install WinUSB driver.cmd" and "Restore Tactrix driver.cmd".
#
# The switch is the same script NC Flash runs, so either one can undo the
# other, and they never run at the same time (the script holds a system-wide
# lock). The release zip ships it unchanged from NC Flash's repository.
#
# The elevated PowerShell re-reads openport_winusb.ps1 and runs it only if its
# SHA-256 still matches the one taken here before the UAC prompt, so the file
# can't be swapped in between.
#
# Exit codes: the script's (0 done, 1 failed, 3 done but restart Windows, 5
# another switch is running), 2 UAC prompt declined, 4 Windows couldn't start
# it, 6 openport_winusb.ps1 changed before it ran.

param([Parameter(Mandatory = $true)][ValidateSet('Install', 'Restore')][string]$Action)

$ErrorActionPreference = 'Stop'
$scriptPath = Join-Path $PSScriptRoot 'openport_winusb.ps1'
$powershell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
$logName = 'standalone_' + [guid]::NewGuid().ToString('N') + '.log'
$logPath = Join-Path $env:ProgramData "NC Flash\OpenPort\$logName"

if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    Write-Host "openport_winusb.ps1 is missing. Extract the whole zip, then run this again."
    exit 1
}
$sha256 = [Security.Cryptography.SHA256]::Create()
$hash = [BitConverter]::ToString($sha256.ComputeHash([IO.File]::ReadAllBytes($scriptPath))) -replace '-', ''

$quotedPath = $scriptPath.Replace("'", "''")
$inner = @"
`$b = [IO.File]::ReadAllBytes('$quotedPath')
`$h = [BitConverter]::ToString([Security.Cryptography.SHA256]::Create().ComputeHash(`$b)) -replace '-', ''
if (`$h -ne '$hash') { exit 6 }
& ([scriptblock]::Create([Text.Encoding]::UTF8.GetString(`$b).TrimStart([char]0xFEFF))) -Action '$Action' -LogName '$logName'
exit 1
"@
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($inner))

if ($Action -eq 'Install') {
    Write-Host 'Putting the Tactrix OpenPort 2.0 on WinUSB. Plug the cable in and close every program that uses it.'
} else {
    Write-Host "Putting the Tactrix OpenPort 2.0 back on Tactrix's driver. Close every program that uses the cable."
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

if (Test-Path -LiteralPath $logPath) {
    Get-Content -LiteralPath $logPath | ForEach-Object { Write-Host "  $_" }
    Write-Host ''
}
$done = if ($Action -eq 'Install') {
    'Done: the cable is on WinUSB. Put j2534.dll and libusb-1.0.dll next to your program (see the README).'
} else {
    "Done: the cable is back on Tactrix's driver. Windows 11 may block it (Code 39 in Device Manager)."
}
switch ($code) {
    0 { Write-Host $done }
    3 { Write-Host "$done Restart Windows to finish." }
    2 { Write-Host 'Cancelled: nothing was changed.' }
    4 { Write-Host 'Windows could not start the setup. Nothing was changed.' }
    5 { Write-Host 'Another driver switch is running (NC Flash?). Wait for it to finish, then try again.' }
    6 { Write-Host 'openport_winusb.ps1 changed while waiting for the prompt, so nothing was run. Extract the zip again.' }
    default { Write-Host "Failed (exit code $code). The log above says why; log file: $logPath" }
}
exit $code
