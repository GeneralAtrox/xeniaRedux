# RAM-guarded, deadline-bounded Xenia build. Kills only its own process tree.
param([int]$Jobs = 8, [int]$DeadlineMin = 95)
$log = Join-Path $PSScriptRoot 'build.log'
$deadline = (Get-Date).AddMinutes($DeadlineMin)
function Ram { $o = Get-CimInstance Win32_OperatingSystem; [math]::Round((1 - $o.FreePhysicalMemory / $o.TotalVisibleMemorySize) * 100, 1) }
$env:VULKAN_SDK = 'C:\msys64\mingw64'  # only glslangValidator/spirv-opt/spirv-dis are used, by full path
$start = Get-Date
"START $start jobs=$Jobs" | Out-File $log -Encoding utf8
$attempt = 0
while ((Get-Date) -lt $deadline) {
  $attempt++
  $p = Start-Process -FilePath python -ArgumentList @('xenia-build.py','build','--config=release','--no_premake','--','-j',"$Jobs") `
       -WorkingDirectory 'C:\Projects\XeniaRedux' -NoNewWindow -PassThru `
       -RedirectStandardOutput (Join-Path $PSScriptRoot "build_out_$attempt.txt") -RedirectStandardError (Join-Path $PSScriptRoot "build_err_$attempt.txt")
  "attempt $attempt pid $($p.Id)" | Out-File $log -Append
  $killed = $false
  while (-not $p.HasExited) {
    Start-Sleep 5
    $r = Ram
    if ($r -ge 95) { "RAM $r >= 95: stopping tree $($p.Id)" | Out-File $log -Append; taskkill /PID $p.Id /T /F | Out-Null; $killed = $true; break }
    if ((Get-Date) -ge $deadline) { "DEADLINE: stopping tree $($p.Id)" | Out-File $log -Append; taskkill /PID $p.Id /T /F | Out-Null; $killed = $true; break }
  }
  if (-not $killed) { $p.WaitForExit(); "EXIT $($p.ExitCode) elapsed $((Get-Date) - $start)" | Out-File $log -Append; exit $p.ExitCode }
  $under = 0
  while ($under -lt 5 -and (Get-Date) -lt $deadline) { Start-Sleep 10; if ((Ram) -lt 85) { $under++ } else { $under = 0 } }
  "resuming (under=$under)" | Out-File $log -Append
}
"GAVE UP at deadline" | Out-File $log -Append
exit 99
