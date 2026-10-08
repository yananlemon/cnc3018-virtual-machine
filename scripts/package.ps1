param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$Destination,
    [string]$Runtime = 'C:\msys64\mingw64'
)
$ErrorActionPreference = 'Stop'
$sourceRoot = Split-Path $PSScriptRoot -Parent
$exePath = (Resolve-Path -LiteralPath $Exe).Path
$bundlePath = [System.IO.Path]::GetFullPath($Destination)
New-Item -ItemType Directory -Force -Path $bundlePath | Out-Null
Copy-Item -LiteralPath $exePath -Destination (Join-Path $bundlePath 'cnc3018_vm.exe')
& (Join-Path $Runtime 'bin\windeployqt.exe') --release --no-translations (Join-Path $bundlePath 'cnc3018_vm.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt deployment failed' }
# Resolve transitive non-system PE imports; the Microsoft Windows DLLs are supplied by Windows.
$queue = [System.Collections.Generic.Queue[string]]::new()
Get-ChildItem -LiteralPath $bundlePath -Recurse -File | Where-Object {$_.Extension -in '.dll','.exe'} | ForEach-Object {$queue.Enqueue($_.FullName)}
$seen = @{}
while ($queue.Count -gt 0) {
    $binary = $queue.Dequeue()
    if ($seen.ContainsKey($binary)) { continue }
    $seen[$binary] = $true
    $imports = & (Join-Path $Runtime 'bin\objdump.exe') -p $binary | Select-String 'DLL Name:'
    foreach ($line in $imports) {
        $name = ($line.Line -split 'DLL Name:')[1].Trim()
        $runtimeDll = Join-Path $Runtime ('bin\' + $name)
        $targetDll = Join-Path $bundlePath $name
        if ((Test-Path -LiteralPath $runtimeDll) -and -not (Test-Path -LiteralPath $targetDll)) {
            Copy-Item -LiteralPath $runtimeDll -Destination $targetDll
            $queue.Enqueue($targetDll)
        }
    }
}
$licenseRoot = Join-Path $bundlePath 'third-party-licenses'
New-Item -ItemType Directory -Force -Path $licenseRoot | Out-Null
$packages = @('qt6-base','gcc-libs','libwinpthread','winpthreads','libb2','double-conversion','icu','pcre2','zlib','zstd','freetype','harfbuzz','md4c','libpng','brotli','libjpeg-turbo','glib2','bzip2','graphite2','gettext-runtime','libffi','libiconv')
foreach ($package in $packages) {
    $licensePath = Join-Path $Runtime ('share\licenses\' + $package)
    if (Test-Path -LiteralPath $licensePath) { Copy-Item -LiteralPath $licensePath -Destination $licenseRoot -Recurse -Force }
}
Get-ChildItem -LiteralPath (Join-Path $Runtime 'share\icu') -Recurse -File -Filter LICENSE -ErrorAction SilentlyContinue | ForEach-Object {
    $icuLicense = Join-Path $licenseRoot 'icu'
    New-Item -ItemType Directory -Force -Path $icuLicense | Out-Null
    Copy-Item -LiteralPath $_.FullName -Destination $icuLicense -Force
}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'README.md'),(Join-Path $sourceRoot 'THIRD_PARTY_NOTICES.md') -Destination $bundlePath
Copy-Item -LiteralPath (Join-Path $sourceRoot 'examples') -Destination $bundlePath -Recurse -Force
Copy-Item -LiteralPath (Join-Path $sourceRoot 'docs') -Destination $bundlePath -Recurse -Force
Write-Output ('Portable bundle: ' + $bundlePath)
