param([string[]]$Images, [string[]]$Labels, [string]$Out, [int]$Cols = 2, [int]$CropX = 8, [int]$CropY = 31, [int]$CropW = 600, [int]$CropH = 540)
# Labelled grid of captures cropped to the game view.
Add-Type -AssemblyName System.Drawing
$gap = 8; $labelH = 28
$rows = [math]::Ceiling($Images.Count / $Cols)
$canvas = New-Object System.Drawing.Bitmap ($Cols * $CropW + ($Cols - 1) * $gap), ($rows * ($CropH + $labelH) + ($rows - 1) * $gap)
$g = [System.Drawing.Graphics]::FromImage($canvas)
$g.Clear([System.Drawing.Color]::FromArgb(20, 20, 24))
$g.InterpolationMode = 'NearestNeighbor'
$font = New-Object System.Drawing.Font("Segoe UI", 11, [System.Drawing.FontStyle]::Bold)
$brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(235, 235, 240))
$src = New-Object System.Drawing.Rectangle $CropX, $CropY, $CropW, $CropH
for ($i = 0; $i -lt $Images.Count; $i++) {
	$col = $i % $Cols; $row = [math]::Floor($i / $Cols)
	$x = $col * ($CropW + $gap); $y = $row * ($CropH + $labelH + $gap)
	$img = [System.Drawing.Bitmap]::FromFile($Images[$i])
	$g.DrawImage($img, (New-Object System.Drawing.Rectangle $x, ($y + $labelH), $CropW, $CropH), $src, [System.Drawing.GraphicsUnit]::Pixel)
	$g.DrawString($Labels[$i], $font, $brush, ($x + 8), ($y + 4))
	$img.Dispose()
}
$canvas.Save($Out)
$Out
