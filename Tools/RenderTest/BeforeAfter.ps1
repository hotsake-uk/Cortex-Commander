param([string]$Before, [string]$After, [string]$Out, [string]$LabelBefore = "BEFORE (development)", [string]$LabelAfter = "AFTER (modernisation)", [int]$CropX = 8, [int]$CropY = 31, [int]$CropW = 600, [int]$CropH = 540)
# Puts two captures side by side (cropped to the game view) with labels.
Add-Type -AssemblyName System.Drawing
# .NET resolves relative paths against the process directory, not the PowerShell location.
$Before = (Resolve-Path $Before).Path
$After = (Resolve-Path $After).Path
if (-not [IO.Path]::IsPathRooted($Out)) { $Out = Join-Path (Get-Location).Path $Out }
$a = [System.Drawing.Bitmap]::FromFile($Before)
$b = [System.Drawing.Bitmap]::FromFile($After)
$gap = 8
$canvas = New-Object System.Drawing.Bitmap ($CropW * 2 + $gap), ($CropH + 28)
$g = [System.Drawing.Graphics]::FromImage($canvas)
$g.Clear([System.Drawing.Color]::FromArgb(20, 20, 24))
$g.InterpolationMode = 'NearestNeighbor'
$src = New-Object System.Drawing.Rectangle $CropX, $CropY, $CropW, $CropH
$g.DrawImage($a, (New-Object System.Drawing.Rectangle 0, 28, $CropW, $CropH), $src, [System.Drawing.GraphicsUnit]::Pixel)
$g.DrawImage($b, (New-Object System.Drawing.Rectangle ($CropW + $gap), 28, $CropW, $CropH), $src, [System.Drawing.GraphicsUnit]::Pixel)
$font = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)
$brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(235, 235, 240))
$g.DrawString($LabelBefore, $font, $brush, 8, 4)
$g.DrawString($LabelAfter, $font, $brush, ($CropW + $gap + 8), 4)
$canvas.Save($Out)
$a.Dispose(); $b.Dispose()
$Out
