param(
    [Parameter(Mandatory=$true)][string]$Distribution,
    [Parameter(Mandatory=$true)][string]$OutputArchive
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = (Resolve-Path -LiteralPath $Distribution).Path
$required = @(
    'GeistLauncher.exe', 'sdk/manifest.ini', 'sdk/recomp/recomp.py',
    'sdk/recomp/dol.py', 'sdk/recomp/ppc.py', 'sdk/symbols.txt',
    'sdk/lib/libgeist_runtime.a', 'sdk/lib/libSDL2.a',
    'toolchain/python/bin/python.exe', 'toolchain/python/bin/libpython3.12.dll',
    'toolchain/python/lib/python3.12/encodings/__init__.py',
    'toolchain/bin/x86_64-w64-mingw32-clang.exe',
    'toolchain/bin/x86_64-w64-mingw32-clang++.exe',
    'toolchain/bin/ld.lld.exe',
    'toolchain/x86_64-w64-mingw32/bin/libc++.dll',
    'toolchain/x86_64-w64-mingw32/bin/libunwind.dll'
)
foreach ($relative in $required) {
    if (!(Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf)) {
        throw "Incomplete release. Missing $relative"
    }
}
$files = @()
foreach ($folder in 'assets', 'licenses', 'sdk', 'toolchain') {
    $files += Get-ChildItem -LiteralPath (Join-Path $root $folder) -File -Recurse
}
foreach ($file in 'GeistLauncher.exe', 'README.txt', 'UPDATE_README.txt', 'TEST_RESULTS.md', 'reporting.ini') {
    $files += Get-Item -LiteralPath (Join-Path $root $file)
}
$files = @($files | Where-Object {
    $_.FullName -notmatch '[\\/]__pycache__[\\/]' -and $_.Extension -ne '.pyc'
})
foreach ($file in $files) {
    if ($file.Extension -in '.iso', '.gcm', '.ciso', '.dol', '.raw', '.gci') {
        throw "Unexpected game or save data in release files. $($file.FullName)"
    }
}
$archivePath = [IO.Path]::GetFullPath($OutputArchive)
if (Test-Path -LiteralPath $archivePath) { throw "Archive already exists. $archivePath" }
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($archivePath)) | Out-Null
$archive = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($file in $files) {
        $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $archive, $file.FullName, "GeistRecompiled/$relative",
            [IO.Compression.CompressionLevel]::Optimal) | Out-Null
    }
} finally {
    $archive.Dispose()
}
"Created complete release with $($files.Count) files."
Get-FileHash -LiteralPath $archivePath -Algorithm SHA256
