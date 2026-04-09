# scripts/png_to_ico.ps1
#
# Tiny PNG -> ICO converter using only System.Drawing (built into
# Windows, no extra dependencies).  Used to regenerate icon.ico
# from the source icon.png whenever the artwork changes.
#
# The output ICO is a single 256x256 PNG-compressed entry with the
# .ico ICONDIRENTRY width/height fields encoded as 0 (which is the
# spec's way of saying "256").  Windows will downscale to whatever
# size is needed at render time (16x16 tray, 32x32 taskbar, etc.) so
# one entry covers everything we need.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File scripts\png_to_ico.ps1
#
# Reads:  icon.png  (project root)
# Writes: icon.ico  (project root)

param(
    [string] $Source = (Join-Path $PSScriptRoot '..\icon.png'),
    [string] $Dest   = (Join-Path $PSScriptRoot '..\icon.ico')
)

Add-Type -AssemblyName System.Drawing

if (-not (Test-Path $Source)) {
    Write-Error "source PNG not found: $Source"
    exit 1
}

# Load and resize to 256x256.
$src = [System.Drawing.Image]::FromFile((Resolve-Path $Source))
$bmp = New-Object System.Drawing.Bitmap 256, 256
$g   = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
$g.DrawImage($src, 0, 0, 256, 256)
$g.Dispose()
$src.Dispose()

# Encode the resized bitmap as PNG into a memory stream.
$png = New-Object System.IO.MemoryStream
$bmp.Save($png, [System.Drawing.Imaging.ImageFormat]::Png)
$bmp.Dispose()
$pngBytes = $png.ToArray()
$png.Dispose()

# Build the ICO container: 6-byte ICONDIR + 16-byte ICONDIRENTRY + PNG data.
$ms = New-Object System.IO.MemoryStream
$bw = New-Object System.IO.BinaryWriter $ms

# ICONDIR
$bw.Write([uint16] 0)               # reserved
$bw.Write([uint16] 1)               # type = 1 (icon)
$bw.Write([uint16] 1)               # count = 1 entry

# ICONDIRENTRY
$bw.Write([byte]   0)               # width (0 means 256)
$bw.Write([byte]   0)               # height (0 means 256)
$bw.Write([byte]   0)               # color count
$bw.Write([byte]   0)               # reserved
$bw.Write([uint16] 1)               # color planes
$bw.Write([uint16] 32)              # bits per pixel
$bw.Write([uint32] $pngBytes.Length) # data size
$bw.Write([uint32] 22)              # data offset (6 + 16)

# Image data
$bw.Write($pngBytes)
$bw.Flush()

$bytes = $ms.ToArray()
$ms.Dispose()
[System.IO.File]::WriteAllBytes($Dest, $bytes)

Write-Host ("Wrote {0} ({1} bytes) from {2}" -f $Dest, $bytes.Length, $Source)
