# preview.ps1 — Launch UzEngine in windowed mode, capture a screenshot, then kill it.
# Usage: powershell -ExecutionPolicy Bypass -File tools/preview.ps1
# Output: tools/preview_capture.png (viewable by the AI agent)

param(
    [int]$DelayMs = 3000,   # Wait time for engine to render first frames
    [string]$ExePath = "$PSScriptRoot\..\build\Release\render.exe",
    [string]$OutPath = "$PSScriptRoot\preview_capture.png",
    [string]$Args = ""
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# Resolve paths
$ExePath = (Resolve-Path $ExePath -ErrorAction Stop).Path
$OutDir = Split-Path $OutPath -Parent
if (!(Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

# Kill any existing UzEngine
Stop-Process -Name UzEngine -Force -ErrorAction SilentlyContinue
Stop-Process -Name render -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 500

# Launch in windowed mode
$env:UZENGINE_WINDOWED = "1"
$env:UZENGINE_HEADLESS = ""
if ($Args) {
    # Split args by space so Start-Process passes them individually
    $ArgArray = $Args -split ' '
    $proc = Start-Process -FilePath $ExePath -ArgumentList $ArgArray -PassThru
} else {
    $proc = Start-Process -FilePath $ExePath -PassThru
}
Write-Host "[preview] Launched UzEngine (PID $($proc.Id)), waiting ${DelayMs}ms..."
Start-Sleep -Milliseconds $DelayMs

if ($proc.HasExited) {
    Write-Host "[preview] ERROR: UzEngine exited early (code $($proc.ExitCode))"
    exit 1
}

# Find the window by PID
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public class Win32 {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    
    public static IntPtr FindWindowByPid(uint pid) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((hWnd, lParam) => {
            uint wpid;
            GetWindowThreadProcessId(hWnd, out wpid);
            if (wpid == pid && IsWindowVisible(hWnd)) { found = hWnd; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@

$hwnd = [Win32]::FindWindowByPid([uint32]$proc.Id)
if ($hwnd -eq [IntPtr]::Zero) {
    Write-Host "[preview] WARNING: Window not found by PID, capturing full screen"
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
} else {
    [Win32]::SetForegroundWindow($hwnd) | Out-Null
    Start-Sleep -Milliseconds 300
    # Use client rect for just the render area (no title bar)
    $clientRect = New-Object Win32+RECT
    [Win32]::GetClientRect($hwnd, [ref]$clientRect) | Out-Null
    $pt = New-Object Win32+POINT
    $pt.X = 0; $pt.Y = 0
    [Win32]::ClientToScreen($hwnd, [ref]$pt) | Out-Null
    $bounds = New-Object System.Drawing.Rectangle($pt.X, $pt.Y, $clientRect.Right, $clientRect.Bottom)
    Write-Host "[preview] Found window at ($($pt.X),$($pt.Y)) size $($clientRect.Right)x$($clientRect.Bottom)"
}

$bmp = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
$gfx.Dispose()
$bmp.Save($OutPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()

# Kill UzEngine
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Write-Host "[preview] Screenshot saved to: $OutPath"
Write-Host "[preview] Done."
