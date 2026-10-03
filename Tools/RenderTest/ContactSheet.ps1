param([string]$Name, [int]$Count = 6, [int]$Cols = 3, [int]$CropX = 8, [int]$CropY = 31, [int]$CropW = 600, [int]$CropH = 540, [double]$Scale = 0.5)
. (Join-Path $PSScriptRoot "Common.ps1")
# Tiles $Name_0..$Name_{Count-1}.png (cropped to the game area) into $Name_sheet.png.
Add-Type -AssemblyName System.Drawing
$tileW = [int]($CropW * $Scale); $tileH = [int]($CropH * $Scale)
$rows = [math]::Ceiling($Count / $Cols)
$sheet = New-Object System.Drawing.Bitmap ($tileW * $Cols), ($tileH * $rows)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.InterpolationMode = 'NearestNeighbor'
for ($i = 0; $i -lt $Count; $i++) {
	$path = (Join-Path $OutputDir "$Name`_$i.png")
	if (-not (Test-Path $path)) { continue }
	$img = [System.Drawing.Bitmap]::FromFile($path)
	$dest = New-Object System.Drawing.Rectangle (($i % $Cols) * $tileW), ([math]::Floor($i / $Cols) * $tileH), $tileW, $tileH
	$src = New-Object System.Drawing.Rectangle $CropX, $CropY, $CropW, $CropH
	$g.DrawImage($img, $dest, $src, [System.Drawing.GraphicsUnit]::Pixel)
	$img.Dispose()
}
$sheetPath = Join-Path $OutputDir "$Name`_sheet.png"
$sheet.Save($sheetPath)
$sheetPath
