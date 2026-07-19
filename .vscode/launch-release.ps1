[CmdletBinding()]
param(
	[ValidateSet('Win32')]
	[string]$Platform = 'Win32',

	[ValidateSet('glx', 'vk', 'rtx')]
	[string]$Renderer = 'glx',

	[ValidateSet('client', 'dedicated')]
	[string]$Target = 'client',

	[string]$BuildDir = $(if ($env:FNQL_MESON_BUILD_DIR) { $env:FNQL_MESON_BUILD_DIR } elseif ($env:FNQ3_MESON_BUILD_DIR) { $env:FNQ3_MESON_BUILD_DIR } else { 'meson\build\win32' })
)

$ErrorActionPreference = 'Stop'

function Get-BinarySuffix {
	if ($Platform -ne 'Win32') {
		throw "The VS Code launch helper supports only the retail-compatible Win32 target."
	}
	return ''
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
