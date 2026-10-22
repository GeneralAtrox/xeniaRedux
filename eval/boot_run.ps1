# Bounded Too Human boot in XeniaRedux: snapshots of Xenia's own window, then kill only our process tree.
param([int]$Seconds = 150, [string]$Tag = 'run1', [string[]]$Extra = @())
$exe = 'C:\Projects\XeniaRedux\build\bin\Windows\Release\xenia_canary.exe'
$iso = 'C:\Projects\TooHuman\Too Human (Japan, Europe)\Too Human (Japan, Europe).iso'
$out = Join-Path $PSScriptRoot $Tag
New-Item -ItemType Directory -Force $out | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
'@
function Ram { $o = Get-CimInstance Win32_OperatingSystem; [math]::Round((1 - $o.FreePhysicalMemory / $o.TotalVisibleMemorySize) * 100, 1) }
function Snap($p, $name) {
  $p.Refresh(); $h = $p.MainWindowHandle; if ($h -eq [IntPtr]::Zero) { return "no window" }
  $r = New-Object W+RECT; [void][W]::GetWindowRect($h, [ref]$r)
  $w = $r.R - $r.L; $hh = $r.B - $r.T; if ($w -le 0) { return "zero rect" }
  $bmp = New-Object System.Drawing.Bitmap $w, $hh; $g = [System.Drawing.Graphics]::FromImage($bmp)
  $dc = $g.GetHdc(); [void][W]::PrintWindow($h, $dc, 2); $g.ReleaseHdc($dc)
  $bmp.Save((Join-Path $out "$name.png")); $g.Dispose(); $bmp.Dispose(); "saved $name ${w}x$hh"
}
$args2 = @("--log_file=`"$out\xenia.log`"", '--apu=nop', '--hid=nop', '--headless=true', '--discord=false') + $Extra + @("`"$iso`"")
$t0 = Get-Date
$p = Start-Process -FilePath $exe -ArgumentList $args2 -WorkingDirectory (Split-Path $exe) -PassThru
"pid $($p.Id) args $args2" | Out-File "$out\run.txt"
$marks = if ($Seconds -gt 200) { @(60,120,180,240,300,360,420,480,540) | Where-Object { $_ -lt $Seconds } } else { @(20, 45, 70, 100, 130, ($Seconds - 5)) }
foreach ($m in $marks) {
  while (((Get-Date) - $t0).TotalSeconds -lt $m) {
    Start-Sleep 2
    if ($p.HasExited) { break }
    $r = Ram; if ($r -ge 95) { "RAM ${r}: stopping" | Out-File "$out\run.txt" -Append; break }
  }
  if ($p.HasExited) { "exited early code $($p.ExitCode) at $([int]((Get-Date)-$t0).TotalSeconds)s" | Out-File "$out\run.txt" -Append; break }
  if ((Ram) -ge 95) { break }
  (Snap $p "t$m") | Out-File "$out\run.txt" -Append
}
if (-not $p.HasExited) { taskkill /PID $p.Id /T /F | Out-Null; "killed at $([int]((Get-Date)-$t0).TotalSeconds)s" | Out-File "$out\run.txt" -Append }
Get-Content "$out\run.txt"
