[CmdletBinding()]
param(
	[ValidateSet('x64', 'Win32', 'ARM64')]
	[string]$Platform = 'x64',

	[ValidateSet('opengl', 'renderer2', 'opengl2', 'vulkan', 'glx')]
	[string]$Renderer = 'opengl',

	[ValidateSet('client', 'dedicated')]
	[string]$Target = 'client',

	[string]$BuildDir = $(if ($env:FNQ3_MESON_BUILD_DIR) { $env:FNQ3_MESON_BUILD_DIR } else { 'meson\build' })
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

function Convert-RendererName {
	switch ($Renderer) {
		'renderer2' { return 'opengl2' }
		default { return $Renderer }
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
	"fnquake3.ded$suffix.exe"
} else {
	"fnquake3$suffix.exe"
}
$executablePath = Join-Path $buildRoot $executableName

if (-not (Test-Path $executablePath)) {
	throw "Executable not found: $executablePath"
}

Set-Location $buildRoot

if ($Target -eq 'dedicated') {
	& $executablePath
} else {
	& $executablePath '+set' 'cl_renderer' (Convert-RendererName)
}
exit $LASTEXITCODE
