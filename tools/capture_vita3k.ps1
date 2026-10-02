# SPDX-License-Identifier: GPL-3.0-only
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class NativeWindows {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr param);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc proc, IntPtr param);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left, Top, Right, Bottom; }
}
"@

$windows = [System.Collections.Generic.List[object]]::new()
$callback = [NativeWindows+EnumProc]{
    param($handle, $parameter)
    $text = [Text.StringBuilder]::new(512)
    [void][NativeWindows]::GetWindowText($handle, $text, $text.Capacity)
    if ($text.Length -gt 0) {
        $processId = [uint32]0
        [void][NativeWindows]::GetWindowThreadProcessId($handle, [ref]$processId)
        $windows.Add([pscustomobject]@{ Handle = $handle; Title = $text.ToString(); ProcessId = $processId })
    }
    return $true
}
[void][NativeWindows]::EnumWindows($callback, [IntPtr]::Zero)
$vita3k = @(Get-Process Vita3K -ErrorAction Stop |
    Where-Object Path -EQ 'D:\Vita3K\Vita3K.exe')
if ($vita3k.Count -eq 0) { throw 'D:\Vita3K\Vita3K.exe is not running' }
$processIds = @($vita3k | ForEach-Object Id)
$target = $windows | Where-Object {
    $processIds -contains $_.ProcessId -and $_.Title -like '*Vita Chips Challenge*'
} | Select-Object -First 1
if (-not $target) {
    $target = $windows | Where-Object { $processIds -contains $_.ProcessId } |
        Select-Object -First 1
}
if (-not $target) {
    $windows | Where-Object Title -Like '*Vita3K*' | Format-Table
    throw 'Vita Chips Challenge window not found'
}
[void][NativeWindows]::SetWindowPos($target.Handle, [IntPtr]::Zero, 0, 0, 976, 583, 0x0040)
[void][NativeWindows]::SetForegroundWindow($target.Handle)
Start-Sleep -Milliseconds 500
$rect = [NativeWindows+Rect]::new()
[void][NativeWindows]::GetWindowRect($target.Handle, [ref]$rect)
$width = $rect.Right - $rect.Left
$height = $rect.Bottom - $rect.Top
$bitmap = [Drawing.Bitmap]::new($width, $height)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
$device = $graphics.GetHdc()
[void][NativeWindows]::PrintWindow($target.Handle, $device, 2)
$graphics.ReleaseHdc($device)
$bitmap.Save('D:\Vita3K\vita-chips-window.png', [Drawing.Imaging.ImageFormat]::Png)
$graphics.Dispose()
$bitmap.Dispose()
$target

# Emulator validation is complete once the evidence image is written. Keep the
# developer's Windows desktop clean and avoid accumulating installer or game
# instances between runs.
Get-Process Vita3K -ErrorAction SilentlyContinue |
    Where-Object Path -EQ 'D:\Vita3K\Vita3K.exe' |
    Stop-Process -Force
