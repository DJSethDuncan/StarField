# CI smoke test: runs StarField.scr full screen and in config mode, verifies it
# stays alive and draws something, and saves screenshots.
param(
    [Parameter(Mandatory)] [string]$Scr,
    [Parameter(Mandatory)] [string]$OutDir
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing, System.Windows.Forms
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

function Save-Screen([string]$path) {
    $bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
    $bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    return $bmp
}

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class Pixels {
    static byte[] Read(Bitmap bmp) {
        var data = bmp.LockBits(new Rectangle(0, 0, bmp.Width, bmp.Height),
                                ImageLockMode.ReadOnly, PixelFormat.Format32bppRgb);
        var bytes = new byte[data.Stride * data.Height];
        Marshal.Copy(data.Scan0, bytes, 0, bytes.Length);
        bmp.UnlockBits(data);
        return bytes;
    }
    // Pixels clearly brighter than the #020405 background.
    public static int Lit(Bitmap bmp) {
        var b = Read(bmp); int n = 0;
        for (int i = 0; i < b.Length; i += 4) if (b[i + 2] > 40) n++;
        return n;
    }
    // Pixels that changed between two frames.
    public static int Changed(Bitmap a, Bitmap c) {
        var x = Read(a); var y = Read(c); int n = 0;
        for (int i = 0; i < x.Length; i += 4) if (Math.Abs(x[i + 2] - y[i + 2]) > 20) n++;
        return n;
    }
}
"@

$failures = 0

# Full-screen mode.
$p = Start-Process -FilePath $Scr -ArgumentList '/s' -PassThru
Start-Sleep -Seconds 5
if ($p.HasExited) {
    Write-Host "FAIL: /s exited early with code $($p.ExitCode)"; $failures++
} else {
    $a = Save-Screen (Join-Path $OutDir 'windows-frame-1.png')
    Start-Sleep -Milliseconds 300
    $b = Save-Screen (Join-Path $OutDir 'windows-frame-2.png')
    $lit = [Pixels]::Lit($a)
    $changed = [Pixels]::Changed($a, $b)
    Write-Host "lit pixels: $lit, changed between frames: $changed"
    # The background is #020405; a mostly-dark screen with some stars is expected.
    $dark = $a.GetPixel([int]($a.Width / 3), 5)
    Write-Host "sample pixel: $($dark.R),$($dark.G),$($dark.B)"
    if ($lit -lt 200) { Write-Host 'FAIL: frame looks empty'; $failures++ }
    if ($changed -lt 50) { Write-Host 'FAIL: frames are not animating'; $failures++ }
    $a.Dispose(); $b.Dispose()
    Stop-Process -Id $p.Id -Force
}

# Settings dialog.
$c = Start-Process -FilePath $Scr -ArgumentList '/c' -PassThru
Start-Sleep -Seconds 2
if ($c.HasExited) {
    Write-Host "FAIL: /c exited early with code $($c.ExitCode)"; $failures++
} else {
    (Save-Screen (Join-Path $OutDir 'windows-config.png')).Dispose()
    Stop-Process -Id $c.Id -Force
}

if ($failures) { Write-Host 'FAILED'; exit 1 }
Write-Host 'OK'
