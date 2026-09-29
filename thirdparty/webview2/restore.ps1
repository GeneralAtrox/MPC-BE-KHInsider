param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$version = '1.0.4258.31'
$expectedHash = '56F7F4B8BF9AEE4B8EFEFBBDD4F67D5F74EBD1B100ED0806DA71BF76AF481AA9'
if (Test-Path -LiteralPath (Join-Path $Destination 'restored.txt')) { exit 0 }
New-Item -ItemType Directory -Path $Destination -Force | Out-Null
$archive = Join-Path $Destination 'sdk.zip'
Invoke-WebRequest -UseBasicParsing -Uri "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$version/microsoft.web.webview2.$version.nupkg" -OutFile $archive
$hash = [Security.Cryptography.SHA256]::Create()
$stream = [IO.File]::OpenRead($archive)
try { $actualHash = [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '') }
finally { $stream.Dispose(); $hash.Dispose() }
if ($actualHash -ne $expectedHash) { throw 'WebView2 SDK checksum mismatch.' }
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
$root = [IO.Path]::GetFullPath($Destination).TrimEnd('\') + '\'
try {
    foreach ($entry in $zip.Entries) {
        $path = [IO.Path]::GetFullPath((Join-Path $root $entry.FullName))
        if (!$path.StartsWith($root, [StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid SDK archive path.' }
        if (!$entry.Name) { [IO.Directory]::CreateDirectory($path) | Out-Null; continue }
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path)) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $path, $true)
    }
} finally { $zip.Dispose() }
[IO.File]::WriteAllText((Join-Path $Destination 'restored.txt'), $version)
Remove-Item -LiteralPath $archive
