[CmdletBinding()]
param(
	[ValidateSet('x64', 'Win32', 'ARM64')]
	[string]$Platform = 'x64',

	[ValidateSet('glx', 'vk', 'rtx')]
	[string]$Renderer = 'glx',

	[ValidateSet('client', 'dedicated')]
	[string]$Target = 'client',

	[string]$BuildDir = $(if ($env:FNQL_MESON_BUILD_DIR) { $env:FNQL_MESON_BUILD_DIR } elseif ($env:FNQ3_MESON_BUILD_DIR) { $env:FNQ3_MESON_BUILD_DIR } else { 'meson\build' })
)

$ErrorActionPreference = 'Stop'

function Get-BinarySuffix {
	switch ($Platform) {
		'Win32' { return '' }
		'x64' { return '.x64' }
		'ARM64' { return '.arm64' }
		default { throw "Unsupported platform: $Platform" }
	}
}

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ([System.IO.Path]::IsPathRooted($BuildDir)) {
	$BuildDir
} else {
	Join-Path $workspaceRoot $BuildDir
}

$suffix = Get-BinarySuffix
$executableName = if ($Target -eq 'dedicated') {
	"fnql.ded$suffix.exe"
} else {
	"fnql$suffix.exe"
}
$executablePath = Join-Path $buildRoot $executableName

if (-not (Test-Path $executablePath)) {
	throw "Executable not found: $executablePath"
}

Set-Location $buildRoot

if ($Target -eq 'dedicated') {
	& $executablePath
} else {
	& $executablePath '+set' 'r_fullscreen' '0' '+set' 'cl_renderer' $Renderer
}
exit $LASTEXITCODE
