# Bounded XeniaRedux run of Too Human: sound off, time limit, RAM guard, and the whole process tree killed at the end.
# Everything the run writes goes to eval\<Tag>\ (log, call log, coverage chunks, snapshots).
param(
  [int]$Seconds = 300,
  [string]$Tag = 'rec1',
  [string]$Buttons = '',            # DP button list, e.g. START@900,A@1500+10 (vblank-keyed)
  [string]$Functions = '',        # call log: hex guest addresses, comma separated, or @file
  [string]$Snapshots = '',        # vblank numbers for guest-output PNGs
  [int]$ExitAfter = 0,            # vblank at which Xenia flushes and exits by itself
  [switch]$NoWindow,
  [switch]$Coverage,
  [string[]]$Extra = @(),
  [string]$Exe = 'C:\Projects\XeniaRedux\build\bin\Windows\Release\xenia_canary.exe',   # another machine's copy, e.g. MAGNUS's
  [string]$Iso = 'C:\Projects\TooHuman\Too Human (Japan, Europe)\Too Human (Japan, Europe).iso',
  [string]$OutRoot = $PSScriptRoot  # where <Tag>\ goes
)
$exe = $Exe
$iso = $Iso
$out = Join-Path $OutRoot $Tag
New-Item -ItemType Directory -Force $out | Out-Null
function Ram { $o = Get-CimInstance Win32_OperatingSystem; [math]::Round((1 - $o.FreePhysicalMemory / $o.TotalVisibleMemorySize) * 100, 1) }
$a = @("--log_file=`"$out\xenia.log`"", '--apu=nop', '--headless=true', '--discord=false')
if ($Buttons) { $a += "--replay_input=`"$Buttons`"" } else { $a += '--hid=nop' }
if ($Functions) { $a += "--call_log_functions=`"$Functions`"", "--call_log_path=`"$out\calllog.bin`"" }
if ($Snapshots) { $a += "--snapshot_vblanks=`"$Snapshots`"", "--snapshot_dir=`"$out`"" }
if ($ExitAfter) { $a += "--exit_after_vblanks=$ExitAfter" }
if ($NoWindow) { $a += '--no_window=true' }
if ($Coverage) { $a += '--trace_functions=true', '--trace_function_coverage=true', "--trace_function_data_path=`"$out\trace.bin`"" }
$a += $Extra + @("`"$iso`"")
$t0 = Get-Date
$p = Start-Process -FilePath $exe -ArgumentList $a -WorkingDirectory (Split-Path $exe) -PassThru
"pid $($p.Id) args $a" | Out-File "$out\run.txt"
$peak = 0
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds -and -not $p.HasExited) {
  Start-Sleep 3
  $r = Ram; if ($r -gt $peak) { $peak = $r }
  if ($r -ge 95) { "RAM ${r}%: stopping at $([int]((Get-Date)-$t0).TotalSeconds)s" | Out-File "$out\run.txt" -Append; break }
}
if ($p.HasExited) { "exited by itself, code $($p.ExitCode), at $([int]((Get-Date)-$t0).TotalSeconds)s" | Out-File "$out\run.txt" -Append }
else { taskkill /PID $p.Id /T /F | Out-Null; "killed (process tree) at $([int]((Get-Date)-$t0).TotalSeconds)s" | Out-File "$out\run.txt" -Append }
"RAM peak $peak%" | Out-File "$out\run.txt" -Append
Get-Content "$out\run.txt"
