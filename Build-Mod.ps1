#requires -Version 7.5
[CmdletBinding()]
param(
    [string]$HellForgeInstall = 'E:\Hellforge\HellForge',
    [string]$TestProject = (Join-Path (Split-Path -Parent $PSScriptRoot) 'SuperEarthDefenseTest'),
    [string]$ZigDir = (Join-Path $HellForgeInstall 'tools\zig'),
    [string]$SdkDir = (Join-Path $HellForgeInstall 'app\sdk')
)

$ErrorActionPreference = 'Stop'
$work = Join-Path $PSScriptRoot '.work'
$output = Join-Path $PSScriptRoot 'build'
$staging = Join-Path $work ('package-' + [Guid]::NewGuid().ToString('N'))
$data = Join-Path $staging 'data'
foreach ($folder in @($work,$output,$data)) { [IO.Directory]::CreateDirectory($folder) | Out-Null }

$presetFile = Join-Path $work 'preset.json'
$baseline = Join-Path $TestProject '.work\war-data-original.json'
& (Join-Path $TestProject 'New-WarSnapshot.ps1') -Baseline $baseline -OutputPath $presetFile -AllCities
$preset = Get-Content -LiteralPath $presetFile -Raw -Encoding UTF8 | ConvertFrom-Json -AsHashtable
function ConvertTo-QuotedRows($Rows) {
    $values = foreach ($entry in $Rows) { ConvertTo-Json -InputObject $entry -Depth 30 -Compress }
    '"' + (($values -join ',').Replace('\', '\\').Replace('"', '\"')) + '"'
}
$header = @(
    '#ifndef SE_WAR_PRESET_H', '#define SE_WAR_PRESET_H',
    ('#define SE_PRESET_CAMPAIGNS ' + (ConvertTo-QuotedRows $preset.status.campaigns)),
    ('#define SE_PRESET_REGIONS ' + (ConvertTo-QuotedRows $preset.status.planetRegions)),
    ('#define SE_PRESET_DEFINITIONS ' + (ConvertTo-QuotedRows $preset.warInfo.planetRegions)),
    '#endif'
)
$header | Set-Content -LiteralPath (Join-Path $work 'war_preset.h') -Encoding ascii

$menu = Join-Path $TestProject 'menu-mod\build\SuperEarthDefenseMenu-0.1.0.zip'
if ((Get-FileHash -LiteralPath $menu -Algorithm SHA256).Hash -ne '539EF53C4F2E9F827BB0B51BF4447422F5662E0B45C8E850EC7DF8F8334B804A') {
    throw 'The menu archive does not match the released menu option.'
}
$zip = [IO.Compression.ZipFile]::OpenRead($menu)
try {
    foreach ($suffix in @('','.stream','.gpu_resources')) {
        $name = '9ba626afa44a3aa3.patch_0' + $suffix
        $entry = $zip.GetEntry('mod/' + $name)
        if (-not $entry) { throw "Missing menu resource $name." }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, (Join-Path $data $name))
    }
} finally { $zip.Dispose() }

$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $work 'zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $work 'zig-cache'
$env:ZIG_LIB_DIR = Join-Path $ZigDir 'lib'
$zig = Join-Path $ZigDir 'zig.exe'
$sources = @(Get-ChildItem -LiteralPath (Join-Path $PSScriptRoot 'src') -Filter '*.c' -File | Select-Object -ExpandProperty FullName)
$dll = Join-Path $staging 'super_earth_defense.dll'
& $zig cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror -shared -I $SdkDir -I $work @sources -o $dll
if ($LASTEXITCODE -ne 0) { throw 'The native HellForge Mod did not compile.' }

foreach ($file in @('hellforge-mod.json','README.md')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $staging }
$sourceOut = Join-Path $staging 'src'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'src') -Destination $sourceOut -Recurse
Copy-Item -LiteralPath (Join-Path $work 'war_preset.h') -Destination $sourceOut
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'hellforge-mod.json') -Encoding utf8 -Raw | ConvertFrom-Json
$archive = Join-Path $output ("SuperEarthDefense-$($manifest.version)-HellForge.zip")
Compress-Archive -LiteralPath @(Get-ChildItem -LiteralPath $staging | Select-Object -ExpandProperty FullName) -DestinationPath $archive -Force
Get-Item -LiteralPath $archive
