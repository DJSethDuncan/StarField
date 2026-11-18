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

function Count-Bright($bmp) {
    $bright = 0
    for ($y = 0; $y -lt $bmp.Height; $y += 4) {
        for ($x = 0; $x -lt $bmp.Width; $x += 4) {
            if ($bmp.GetPixel($x, $y).R -gt 60) { $bright++ }
        }
    }
    return $bright
}

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
    $brightA = Count-Bright $a
    Write-Host "bright samples: $brightA"
    # The background is #020405; a mostly-dark screen with some stars is expected.
    $dark = $a.GetPixel([int]($a.Width / 3), 5)
    Write-Host "sample pixel: $($dark.R),$($dark.G),$($dark.B)"
    if ($brightA -lt 20) { Write-Host 'FAIL: frame looks empty'; $failures++ }
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
