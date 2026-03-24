# sdcmd.ps1 — Send a debug command to a running SmartDashboardApp instance
# Usage: powershell -File sdcmd.ps1 <ProcessId> <command...>
# Example: powershell -File sdcmd.ps1 25132 publish double TestMove 3.5
# Ian: Do NOT name the parameter $Pid — it shadows PowerShell's automatic
# $PID variable and causes "Cannot overwrite variable Pid" errors.

param(
    [Parameter(Mandatory=$true)][int]$ProcessId,
    [Parameter(Mandatory=$true, ValueFromRemainingArguments=$true)][string[]]$CommandParts
)

$command = $CommandParts -join ' '
$pipeName = "SmartDashboardApp_DebugCmd_$ProcessId"

Write-Host "Connecting to pipe: $pipeName"
Write-Host "Sending command: $command"

$pipe = New-Object System.IO.Pipes.NamedPipeClientStream('.', $pipeName, [System.IO.Pipes.PipeDirection]::InOut)
$pipe.Connect(2000)

$sw = New-Object System.IO.StreamWriter($pipe)
$sr = New-Object System.IO.StreamReader($pipe)

$sw.WriteLine($command)
$sw.Flush()

$response = $sr.ReadLine()
Write-Host "Response: $response"

$pipe.Close()
