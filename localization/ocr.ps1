# Windows OCR over every PNG in a folder: writes one line per recognised word (UTF-8) to -Out:
#   <file name> `t <x> `t <y> `t <width> `t <height> `t <text>
# (a word's box lets the caller tell which glyph of a row each character came from).
#   powershell -NoProfile -File ocr.ps1 -Dir out\font\ocr -Out out\font\ocr.tsv [-Lang ja]
# Needs the language's OCR component (Settings > Time & Language > Language: Japanese).
param([Parameter(Mandatory)][string]$Dir, [Parameter(Mandatory)][string]$Out, [string]$Lang = "ja")

Add-Type -AssemblyName System.Runtime.WindowsRuntime
[void][Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
[void][Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime]
[void][Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics, ContentType = WindowsRuntime]
[void][Windows.Globalization.Language, Windows.Globalization, ContentType = WindowsRuntime]

$asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
function Await($op, [type]$type) {
    $t = $asTask.MakeGenericMethod($type).Invoke($null, @($op))
    $t.Wait() | Out-Null
    $t.Result
}

$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage([Windows.Globalization.Language]::new($Lang))
if (-not $engine) { throw "no OCR engine for $Lang" }
$lines = New-Object System.Collections.Generic.List[string]
foreach ($f in Get-ChildItem -LiteralPath $Dir -Filter *.png | Sort-Object Name) {
    $file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($f.FullName)) ([Windows.Storage.StorageFile])
    $stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
    $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
    $bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
    $result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
    foreach ($line in $result.Lines) {
        foreach ($w in $line.Words) {
            $r = $w.BoundingRect
            $lines.Add("$($f.Name)`t$([int]$r.X)`t$([int]$r.Y)`t$([int]$r.Width)`t$([int]$r.Height)`t$($w.Text)")
        }
    }
    $stream.Dispose()
}
[IO.File]::WriteAllLines((Resolve-Path -LiteralPath (Split-Path $Out -Parent)).Path + "\" + (Split-Path $Out -Leaf), $lines, [Text.UTF8Encoding]::new($false))
"$($lines.Count) words read"
