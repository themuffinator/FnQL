[CmdletBinding()]
param(
	[ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
	[string]$Configuration = 'Release',

	[ValidateSet('x64', 'Win32', 'ARM64')]
	[string]$Platform = 'x64',

	[ValidateSet('client', 'dedicated', 'both')]
	[string]$Target = 'both',

	[ValidateSet('all', 'opengl', 'renderer2', 'opengl2', 'vulkan', 'glx')]
	[string]$Renderer = 'all',

	[string]$Renderers = $(if ($env:FNQ3_MESON_RENDERERS) { $env:FNQ3_MESON_RENDERERS } else { 'opengl,glx,vulkan,opengl2' }),
	[ValidateSet('opengl', 'opengl2', 'vulkan', 'glx')]
	[string]$RendererDefault = $(if ($env:FNQ3_MESON_RENDERER_DEFAULT) { $env:FNQ3_MESON_RENDERER_DEFAULT } else { 'opengl' }),
	[string]$BuildDir = $(if ($env:FNQ3_MESON_BUILD_DIR) { $env:FNQ3_MESON_BUILD_DIR } else { 'meson\build' }),
	[switch]$SetupOnly,
	[switch]$RunTests,
	[switch]$Install,
	[string]$DestDir = $(if ($env:FNQ3_MESON_DESTDIR) { $env:FNQ3_MESON_DESTDIR } else { '' }),
	[switch]$Archive,
	[string]$RootArchiveName = $(if ($env:FNQ3_MESON_ROOT_ARCHIVE_NAME) { $env:FNQ3_MESON_ROOT_ARCHIVE_NAME } else { 'FnQuake3-pkg.fnz' })
)

$ErrorActionPreference = 'Stop'

function Convert-BuildType {
	param([string]$SelectedConfiguration)

	switch ($SelectedConfiguration) {
		'Debug' { return 'debug' }
		'RelWithDebInfo' { return 'debugoptimized' }
		default { return 'release' }
	}
}

function Resolve-CommandPath {
	param(
		[string]$Name,
		[string]$Override
	)

	if ($Override) {
		if (Test-Path $Override) {
			return (Resolve-Path $Override).Path
		}
		throw "$Name path does not exist: $Override"
	}

	$cmd = Get-Command $Name -ErrorAction SilentlyContinue
	if ($cmd) {
		return $cmd.Source
	}

	throw "Unable to locate $Name. Install Meson/Ninja or add them to PATH."
}

function Convert-RendererName {
	param([string]$SelectedRenderer)

	switch ($SelectedRenderer) {
		'renderer2' { return 'opengl2' }
		default { return $SelectedRenderer }
	}
}

function Resolve-RendererList {
	param(
		[string]$SelectedRenderer,
		[string]$RendererCsv,
		[bool]$RendererCsvWasProvided
	)

	if ($RendererCsvWasProvided -or $SelectedRenderer -eq 'all') {
		$items = $RendererCsv -split ','
	} else {
		$requested = Convert-RendererName -SelectedRenderer $SelectedRenderer
		$items = @('opengl', $requested)
	}

	$result = New-Object System.Collections.Generic.List[string]
	foreach ($item in $items) {
		$rendererName = Convert-RendererName -SelectedRenderer ($item.Trim())
		if (-not $rendererName) {
			continue
		}
		if ($rendererName -notin @('opengl', 'glx', 'vulkan', 'opengl2')) {
			throw "Unsupported renderer module: $rendererName"
		}
		if (-not $result.Contains($rendererName)) {
			$result.Add($rendererName)
		}
	}

	if ($result.Count -eq 0) {
		throw 'At least one renderer module must be selected.'
	}
	return ($result -join ',')
}

function Resolve-BuildPath {
	param(
		[string]$WorkspaceRoot,
		[string]$SelectedBuildDir
	)

	if ([System.IO.Path]::IsPathRooted($SelectedBuildDir)) {
		return $SelectedBuildDir
	}
	return (Join-Path $WorkspaceRoot $SelectedBuildDir)
}

function Get-ExpectedBinarySuffix {
	param([string]$SelectedPlatform)

	switch ($SelectedPlatform) {
		'Win32' { return '' }
		'x64' { return '.x64' }
		'ARM64' { return '.arm64' }
		default { throw "Unsupported platform: $SelectedPlatform" }
	}
}

function Get-ExpectedRendererArch {
	param([string]$SelectedPlatform)

	switch ($SelectedPlatform) {
		'Win32' { return 'x86' }
		'x64' { return 'x86_64' }
		'ARM64' { return 'arm64' }
		default { throw "Unsupported platform: $SelectedPlatform" }
	}
}

function Assert-WindowsOutputs {
	param(
		[string]$BuildPath,
		[string]$SelectedPlatform,
		[string]$SelectedTarget,
		[string]$RendererCsv
	)

	if ($env:OS -ne 'Windows_NT') {
		return
	}

	$suffix = Get-ExpectedBinarySuffix -SelectedPlatform $SelectedPlatform
	$rendererArch = Get-ExpectedRendererArch -SelectedPlatform $SelectedPlatform

	if ($SelectedTarget -in @('client', 'both')) {
		$clientExe = Join-Path $BuildPath "fnquake3$suffix.exe"
		if (-not (Test-Path $clientExe)) {
			throw "Client executable was not produced: $clientExe"
		}

		foreach ($rendererName in ($RendererCsv -split ',')) {
			$rendererDll = Join-Path $BuildPath "fnquake3_${rendererName}_${rendererArch}.dll"
			if (-not (Test-Path $rendererDll)) {
				throw "Renderer module was not produced: $rendererDll"
			}
		}
	}

	if ($SelectedTarget -in @('dedicated', 'both')) {
		$dedicatedExe = Join-Path $BuildPath "fnquake3.ded$suffix.exe"
		if (-not (Test-Path $dedicatedExe)) {
			throw "Dedicated executable was not produced: $dedicatedExe"
		}
	}
}

function Clear-StaleMesonArchive {
	param(
		[string]$BuildPath,
		[string]$ArchiveName
	)

	$archivePath = Join-Path $BuildPath $ArchiveName
	if (Test-Path $archivePath) {
		Remove-Item -LiteralPath $archivePath -Force
	}
}

function Invoke-MesonInstall {
	param(
		[string]$MesonPath,
		[string]$BuildPath,
		[string]$SelectedDestDir,
		[string]$InstallTags = ''
	)

	$installArgs = @('install', '-C', $BuildPath, '--no-rebuild')
	if ($SelectedDestDir) {
		$installArgs += @('--destdir', $SelectedDestDir)
	}
	if ($InstallTags) {
		$installArgs += @('--tags', $InstallTags)
	}
	Write-Host "==> $MesonPath $($installArgs -join ' ')"
	& $MesonPath @installArgs
	if ($LASTEXITCODE -ne 0) {
		throw 'Meson install failed.'
	}
}

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Resolve-BuildPath -WorkspaceRoot $workspaceRoot -SelectedBuildDir $BuildDir
$buildType = Convert-BuildType -SelectedConfiguration $Configuration
$rendererCsv = Resolve-RendererList `
	-SelectedRenderer $Renderer `
	-RendererCsv $Renderers `
	-RendererCsvWasProvided ($PSBoundParameters.ContainsKey('Renderers'))
$mesonPath = Resolve-CommandPath -Name 'meson' -Override $env:FNQ3_MESON

Write-Host "Meson: $mesonPath"
Write-Host "Configuration: $Configuration ($buildType)"
Write-Host "Platform: $Platform"
Write-Host "Target: $Target"
Write-Host "Build directory: $buildPath"
Write-Host "Renderer modules: $rendererCsv"
Write-Host "Renderer default: $RendererDefault"
if ($Archive) {
	Write-Host "Root package archive: $(Join-Path $buildPath $RootArchiveName)"
}

$coreDataPath = Join-Path $buildPath 'meson-private\coredata.dat'
$setupArgs = @(
	'setup',
	$buildPath,
	"--buildtype=$buildType",
	'-Drenderer-dlopen=true',
	"-Drenderers=$rendererCsv",
	"-Drenderer-default=$RendererDefault",
	'-Dogg:default_library=static'
)

if ($Target -eq 'client') {
	$setupArgs += @('-Dbuild-client=true', '-Dbuild-server=false')
} elseif ($Target -eq 'dedicated') {
	$setupArgs += @('-Dbuild-client=false', '-Dbuild-server=true')
} else {
	$setupArgs += @('-Dbuild-client=true', '-Dbuild-server=true')
}

if (Test-Path $coreDataPath) {
	$setupArgs = @('setup', '--reconfigure') + $setupArgs[1..($setupArgs.Count - 1)]
}

Write-Host "==> $mesonPath $($setupArgs -join ' ')"
& $mesonPath @setupArgs
if ($LASTEXITCODE -ne 0) {
	throw 'Meson setup failed.'
}

if ($SetupOnly) {
	Write-Host 'Meson setup completed.'
	exit 0
}

Clear-StaleMesonArchive -BuildPath $buildPath -ArchiveName 'libbotlib.a'
Clear-StaleMesonArchive -BuildPath $buildPath -ArchiveName $RootArchiveName

$compileArgs = @('compile', '-C', $buildPath)
Write-Host "==> $mesonPath $($compileArgs -join ' ')"
& $mesonPath @compileArgs
if ($LASTEXITCODE -ne 0) {
	throw 'Meson compile failed.'
}

Assert-WindowsOutputs -BuildPath $buildPath -SelectedPlatform $Platform -SelectedTarget $Target -RendererCsv $rendererCsv

if ($RunTests) {
	$testArgs = @('test', '-C', $buildPath, '--print-errorlogs')
	Write-Host "==> $mesonPath $($testArgs -join ' ')"
	& $mesonPath @testArgs
	if ($LASTEXITCODE -ne 0) {
		throw 'Meson tests failed.'
	}
}

if ($Install) {
	Invoke-MesonInstall -MesonPath $mesonPath -BuildPath $buildPath -SelectedDestDir $DestDir
}

if ($Archive) {
	$archivePath = Join-Path $buildPath $RootArchiveName
	if (-not (Test-Path $archivePath)) {
		throw "Root package archive was not produced: $archivePath"
	}
	$verifyScript = Join-Path $workspaceRoot 'scripts\verify_release_layout.py'
	Write-Host "==> python $verifyScript $archivePath"
	& python $verifyScript $archivePath
	if ($LASTEXITCODE -ne 0) {
		throw 'Root package archive verification failed.'
	}

	Write-Host "Root package archive ready: $archivePath"
}

Write-Host 'Meson release build completed.'
