#requires -Version 7.5
[CmdletBinding()]
param(
    [string]$HellForgeInstall = 'E:\Hellforge\HellForge',
    [string]$TestProject = (Join-Path (Split-Path -Parent $PSScriptRoot) 'SuperEarthDefenseTest'),
    [string]$ZigDir = (Join-Path $HellForgeInstall 'tools\zig'),
    [string]$SdkDir = (Join-Path $HellForgeInstall 'app\sdk'),
    [switch]$MixedFactionTest
)

$ErrorActionPreference = 'Stop'
$work = Join-Path $PSScriptRoot '.work'
$output = Join-Path $PSScriptRoot 'build'
$staging = Join-Path $work ('package-' + [Guid]::NewGuid().ToString('N'))
$data = Join-Path $staging 'data'
foreach ($folder in @($work,$output,$data)) { [IO.Directory]::CreateDirectory($folder) | Out-Null }

if ($MixedFactionTest) { Write-Host 'The default build now includes the city faction menu.' }
$presetDir = [IO.Directory]::CreateDirectory((Join-Path $work ('preset-' + [Guid]::NewGuid().ToString('N')))).FullName
$presetFile = Join-Path $presetDir 'preset.json'
$baseline = Join-Path $TestProject '.work\war-data-original.json'
& (Join-Path $TestProject 'New-WarSnapshot.ps1') -Baseline $baseline -OutputPath $presetFile -AllCities
$preset = Get-Content -LiteralPath $presetFile -Raw -Encoding UTF8 | ConvertFrom-Json -AsHashtable
function ConvertTo-QuotedRows($Rows) {
    $values = foreach ($entry in $Rows) { ConvertTo-Json -InputObject $entry -Depth 30 -Compress }
    '"' + (($values -join ',').Replace('\', '\\').Replace('"', '\"')) + '"'
}
$cityRows = @(
    foreach ($index in 0..6) {
        $region = @($preset.status.planetRegions | Where-Object { $_.regionIndex -eq $index })
        $definition = @($preset.warInfo.planetRegions | Where-Object { $_.regionIndex -eq $index })
        if ($region.Count -ne 1 -or $definition.Count -ne 1 -or -not $region[0].isAvailable -or $region[0].owner -ne 1) {
            throw "Unexpected Super Earth region $index."
        }
        $choices = foreach ($faction in 2,3,4) {
            $row = [ordered]@{}
            foreach ($key in $region[0].Keys) { $row[$key] = $region[0][$key] }
            if ($faction -ne 4) {
                $row.owner = $faction
                $row.regerPerSecond = [single]($definition[0].maxHealth / 360000.0)
            }
            ConvertTo-QuotedRows @($row)
        }
        '    {' + ($choices -join ', ') + '},'
    }
)
$header = @(
    '#ifndef SE_WAR_PRESET_H', '#define SE_WAR_PRESET_H',
    ('#define SE_PRESET_CAMPAIGNS ' + (ConvertTo-QuotedRows $preset.status.campaigns)),
    ('#define SE_PRESET_DEFINITIONS ' + (ConvertTo-QuotedRows $preset.warInfo.planetRegions)),
    'static const char *const se_city_rows[7][3] = {'
    $cityRows
    '};'
    '#endif'
)
$headerFile = Join-Path $presetDir 'war_preset.h'
$header | Set-Content -LiteralPath $headerFile -Encoding ascii

$menuStage = [IO.Directory]::CreateDirectory((Join-Path $presetDir 'menu')).FullName
$menuSrc = [IO.Directory]::CreateDirectory((Join-Path $menuStage 'src')).FullName
$menuVersion = '0.4.3'
$menuTemplate = Join-Path (Split-Path -Parent $PSScriptRoot) '.tools\template\build.ps1'
Copy-Item -LiteralPath $menuTemplate -Destination (Join-Path $menuStage 'build.ps1')
Copy-Item -LiteralPath (Join-Path $TestProject 'menu-mod\hd2runtime.json') -Destination $menuStage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination $menuStage
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'menu\addon.lua') -Destination $menuSrc
Set-Content -LiteralPath (Join-Path $menuStage 'VERSION') -Value $menuVersion -Encoding ascii
& (Join-Path $menuStage 'build.ps1')
$menu = Join-Path $menuStage "build\SuperEarthDefenseMenu-$menuVersion.zip"
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
& $zig cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror -shared -I $SdkDir -I $presetDir @sources -o $dll
if ($LASTEXITCODE -ne 0) { throw 'The native HellForge Mod did not compile.' }

$manifestPath = Join-Path $staging 'hellforge-mod.json'
foreach ($file in @('hellforge-mod.json','README.md')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $staging }
$luaOut = [IO.Directory]::CreateDirectory((Join-Path $staging 'Source\LooseLua')).FullName
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'menu\addon.lua') -Destination $luaOut
$sourceOut = Join-Path $staging 'src'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'src') -Destination $sourceOut -Recurse
Copy-Item -LiteralPath $headerFile -Destination $sourceOut
$manifest = Get-Content -LiteralPath $manifestPath -Encoding utf8 -Raw | ConvertFrom-Json
$archive = Join-Path $output ("SuperEarthDefense-$($manifest.version)-HellForge.zip")
$members = @($manifestPath, (Join-Path $staging 'README.md'), $dll, $data, $sourceOut, (Split-Path -Parent $luaOut))
Compress-Archive -LiteralPath $members -DestinationPath $archive -Force
Get-Item -LiteralPath $archive
