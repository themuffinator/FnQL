<#
Builds the companion FnQL-game checkout (Win32 x86) and stages its modules
into a development homepath that the "FnQL-game" launch configurations use.
The retail install stays untouched: modules load from <homepath>\baseq3.
#>
param(
	[string]$GameRoot = (Join-Path $PSScriptRoot '..\..\FnQL-game'),
	[string]$BuildDir = 'build\win32-x86',
	[string]$HomePath = (Join-Path $PSScriptRoot '..\.tmp\fnql-game-home')
)

$ErrorActionPreference = 'Stop'

$GameRoot = [System.IO.Path]::GetFullPath($GameRoot)
if (-not (Test-Path (Join-Path $GameRoot 'meson.build'))) {
	throw "FnQL-game checkout not found at $GameRoot (pass -GameRoot)."
}
$build = Join-Path $GameRoot $BuildDir

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
	$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	if (-not (Test-Path $vswhere)) {
		throw 'Visual Studio discovery tool was not found and no MSVC environment is active.'
	}
	$installation = & $vswhere -latest -products * `
		-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
	if (-not $installation) {
		throw 'A Visual Studio installation with the MSVC x86/x64 tools was not found.'
	}
	$devCmd = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
	$env:VSCMD_SKIP_SENDTELEMETRY = '1'
	$lines = & cmd.exe /d /s /c "`"$devCmd`" -no_logo -arch=x86 -host_arch=x64 >nul && set"
	if ($LASTEXITCODE -ne 0) {
		throw 'Visual Studio x86 environment setup failed.'
	}
	foreach ($line in $lines) {
		if ($line -match '^([^=]+)=(.*)$') {
			Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
		}
	}
}

if (-not (Test-Path (Join-Path $build 'build.ninja'))) {
	& meson setup $build $GameRoot
	if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& meson compile -C $build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$dest = Join-Path ([System.IO.Path]::GetFullPath($HomePath)) 'baseq3'
New-Item -ItemType Directory -Force $dest | Out-Null
foreach ($name in 'fnql_qagamex86.dll', 'fnql_cgamex86.dll', 'fnql_uix86.dll', 'fnql_game_client.pk3') {
	$source = Join-Path $build $name
	if (Test-Path $source) {
		Copy-Item -Force $source $dest
	} elseif ($name -like '*.dll') {
		throw "FnQL-game build did not produce $source."
	}
}
Write-Host "Staged FnQL-game modules in $dest"
