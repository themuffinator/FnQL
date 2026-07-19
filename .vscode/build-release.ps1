[CmdletBinding()]
param(
	[ValidateSet('Debug', 'Release', 'RelWithDebInfo')]
	[string]$Configuration = 'Release',

	[ValidateSet('Win32')]
	[string]$Platform = 'Win32',

	[ValidateSet('client', 'dedicated', 'both')]
	[string]$Target = 'both',

	[ValidateSet('all', 'glx', 'vk', 'rtx')]
	[string]$Renderer = 'all',

	[string]$Renderers = $(if ($env:FNQL_MESON_RENDERERS) { $env:FNQL_MESON_RENDERERS } elseif ($env:FNQ3_MESON_RENDERERS) { $env:FNQ3_MESON_RENDERERS } else { 'glx,vk,rtx' }),
	[ValidateSet('glx', 'vk', 'rtx')]
	[string]$RendererDefault = $(if ($env:FNQL_MESON_RENDERER_DEFAULT) { $env:FNQL_MESON_RENDERER_DEFAULT } elseif ($env:FNQ3_MESON_RENDERER_DEFAULT) { $env:FNQ3_MESON_RENDERER_DEFAULT } else { 'glx' }),
	[string]$BuildDir = $(if ($env:FNQL_MESON_BUILD_DIR) { $env:FNQL_MESON_BUILD_DIR } elseif ($env:FNQ3_MESON_BUILD_DIR) { $env:FNQ3_MESON_BUILD_DIR } else { 'meson\build\win32' }),
	[switch]$SetupOnly,
	[switch]$RunTests,
	[switch]$Install,
	[string]$DestDir = $(if ($env:FNQL_MESON_DESTDIR) { $env:FNQL_MESON_DESTDIR } elseif ($env:FNQ3_MESON_DESTDIR) { $env:FNQ3_MESON_DESTDIR } else { '' }),
	[switch]$Archive,
	[string]$RootArchiveName = $(if ($env:FNQL_MESON_ROOT_ARCHIVE_NAME) { $env:FNQL_MESON_ROOT_ARCHIVE_NAME } elseif ($env:FNQ3_MESON_ROOT_ARCHIVE_NAME) { $env:FNQ3_MESON_ROOT_ARCHIVE_NAME } else { 'FnQL-pkg.fnz' }),
	[switch]$WithSteam,
	[string]$SteamRepo = $(if ($env:FNQL_STEAM_REPO) { $env:FNQL_STEAM_REPO } else { '..\FnQL-Steam' })
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

function Import-MsvcEnvironment {
	param([string]$SelectedPlatform)

	if ($env:OS -ne 'Windows_NT') {
		return
	}

	if ($SelectedPlatform -ne 'Win32') {
		throw "The VS Code build helper supports only the retail-compatible Win32 target."
	}
	$arch = 'x86'

	$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	if (-not (Test-Path $vswhere)) {
		if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
			return
		}
		throw 'Visual Studio discovery tool was not found and no MSVC environment is active.'
	}

	$installation = & $vswhere -latest -products * `
		-requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
	if (-not $installation) {
		throw 'A Visual Studio installation with the MSVC x86/x64 tools was not found.'
	}

	$devCmd = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
	$environmentCommand = "`"$devCmd`" -no_logo -arch=$arch -host_arch=x64 >nul && set"
	$environmentLines = & cmd.exe /d /s /c $environmentCommand
	if ($LASTEXITCODE -ne 0) {
		throw "Visual Studio environment setup failed for $SelectedPlatform."
	}

	foreach ($line in $environmentLines) {
		if ($line -match '^([^=]+)=(.*)$') {
			Set-Item -Path "Env:$($matches[1])" -Value $matches[2]
		}
	}
}

function Resolve-RendererList {
	param(
		[string]$SelectedRenderer,
		[string]$SelectedDefault,
		[string]$RendererCsv,
		[bool]$RendererCsvWasProvided
	)

	if ($RendererCsvWasProvided -or $SelectedRenderer -eq 'all') {
		$items = $RendererCsv -split ','
	} else {
		$items = @($SelectedDefault, $SelectedRenderer)
	}

	$result = New-Object System.Collections.Generic.List[string]
	foreach ($item in $items) {
		$rendererName = $item.Trim()
		if (-not $rendererName) {
			continue
		}
		if ($rendererName -notin @('glx', 'vk', 'rtx')) {
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

	if ($SelectedPlatform -ne 'Win32') {
		throw "The VS Code build helper supports only the retail-compatible Win32 target."
	}
	return ''
}

function Get-ExpectedRendererArch {
	param([string]$SelectedPlatform)

	if ($SelectedPlatform -ne 'Win32') {
		throw "The VS Code build helper supports only the retail-compatible Win32 target."
	}
	return 'x86'
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
		$clientExe = Join-Path $BuildPath "fnql$suffix.exe"
		if (-not (Test-Path $clientExe)) {
			throw "Client executable was not produced: $clientExe"
		}

		foreach ($rendererName in ($RendererCsv -split ',')) {
			$rendererDll = Join-Path $BuildPath "fnql_${rendererName}_${rendererArch}.dll"
			if (-not (Test-Path $rendererDll)) {
				throw "Renderer module was not produced: $rendererDll"
			}
		}
	}

	if ($SelectedTarget -in @('dedicated', 'both')) {
		$dedicatedExe = Join-Path $BuildPath "fnql.ded$suffix.exe"
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

function Remove-StaleNativeTestBinaries {
	param(
		[string]$SelectedBuildPath,
		[switch]$Recurse
	)

	if (-not (Test-Path -LiteralPath $SelectedBuildPath)) {
		return
	}
	$searchArgs = @{
		LiteralPath = $SelectedBuildPath
		File = $true
		ErrorAction = 'SilentlyContinue'
	}
	if ($Recurse) {
		$searchArgs.Recurse = $true
	}
	$staleTests = @(Get-ChildItem @searchArgs | Where-Object {
		$_.BaseName -like 'fnql*_tests' -and $_.Extension -in @('.exe', '.pdb')
	})
	foreach ($staleTest in $staleTests) {
		Remove-Item -LiteralPath $staleTest.FullName -Force
	}
	if ($staleTests) {
		Write-Host "Removed $($staleTests.Count) stale native test artifact(s) from $SelectedBuildPath"
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

function Invoke-FnQLSteamBuild {
	param(
		[string]$Repository,
		[string]$SelectedPlatform,
		[string]$SelectedConfiguration,
		[string]$FnQLRoot,
		[string]$BuildPath
	)

	if ($SelectedPlatform -ne 'Win32') {
		throw "The VS Code Steam build supports only the retail-compatible Win32 target."
	}
	$repositoryPath = (Resolve-Path $Repository -ErrorAction Stop).Path
	$cmakePath = Resolve-CommandPath -Name 'cmake' -Override ''
	$providerBuildPath = Join-Path $repositoryPath 'build\vscode-win32'
	Remove-StaleNativeTestBinaries -SelectedBuildPath $providerBuildPath -Recurse
	Write-Host "==> FnQL-Steam $SelectedPlatform $SelectedConfiguration"
	& $cmakePath -S $repositoryPath -B $providerBuildPath -A Win32 `
		"-DFNQL_ROOT=$FnQLRoot" '-DBUILD_TESTING=OFF'
	if ($LASTEXITCODE -ne 0) {
		throw 'FnQL-Steam provider configuration failed.'
	}
	& $cmakePath --build $providerBuildPath --config $SelectedConfiguration --target fnql_steam
	if ($LASTEXITCODE -ne 0) {
		throw 'FnQL-Steam provider build failed.'
	}
	$provider = Join-Path $providerBuildPath "$SelectedConfiguration\fnql_steam.dll"
	if (-not (Test-Path -LiteralPath $provider)) {
		throw "FnQL-Steam provider was not produced: $provider"
	}
	Copy-Item -LiteralPath $provider -Destination (Join-Path $BuildPath 'fnql_steam.dll') -Force
	Write-Host "Staged FnQL-Steam provider beside the engine: $(Join-Path $BuildPath 'fnql_steam.dll')"
}

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Resolve-BuildPath -WorkspaceRoot $workspaceRoot -SelectedBuildDir $BuildDir
$canonicalRootArchiveName = 'FnQL-pkg.fnz'
if ([System.IO.Path]::GetFileName($RootArchiveName) -ne $RootArchiveName -or
	[System.IO.Path]::GetExtension($RootArchiveName) -ne '.fnz') {
	throw "Root archive name must be a .fnz file name without a directory: $RootArchiveName"
}
$buildType = Convert-BuildType -SelectedConfiguration $Configuration
$rendererCsv = Resolve-RendererList `
	-SelectedRenderer $Renderer `
	-SelectedDefault $RendererDefault `
	-RendererCsv $Renderers `
	-RendererCsvWasProvided ($PSBoundParameters.ContainsKey('Renderers'))
$mesonOverride = if ($env:FNQL_MESON) { $env:FNQL_MESON } else { $env:FNQ3_MESON }
Import-MsvcEnvironment -SelectedPlatform $Platform
$mesonPath = Resolve-CommandPath -Name 'meson' -Override $mesonOverride

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
	'-Dstrict-warnings=true',
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

$isReconfigure = Test-Path $coreDataPath
if ($isReconfigure) {
	$setupArgs = @('setup', '--reconfigure') + $setupArgs[1..($setupArgs.Count - 1)]
}

Write-Host "==> $mesonPath $($setupArgs -join ' ')"
& $mesonPath @setupArgs
if ($LASTEXITCODE -ne 0) {
	if (-not $isReconfigure) {
		throw 'Meson setup failed.'
	}
	# Reconfigure fails when coredata holds a stale option definition (e.g. an
	# option changed type between pulls); a wipe rebuilds the configuration.
	$wipeArgs = @('setup', '--wipe') + $setupArgs[2..($setupArgs.Count - 1)]
	Write-Host 'Meson reconfigure failed; retrying with a clean configuration (--wipe).'
	Write-Host "==> $mesonPath $($wipeArgs -join ' ')"
	& $mesonPath @wipeArgs
	if ($LASTEXITCODE -ne 0) {
		throw 'Meson setup failed.'
	}
}

if ($SetupOnly) {
	Write-Host 'Meson setup completed.'
	exit 0
}

Clear-StaleMesonArchive -BuildPath $buildPath -ArchiveName 'libbotlib.a'
Clear-StaleMesonArchive -BuildPath $buildPath -ArchiveName $canonicalRootArchiveName
Remove-StaleNativeTestBinaries -SelectedBuildPath $buildPath
if ($RootArchiveName -ne $canonicalRootArchiveName) {
	Clear-StaleMesonArchive -BuildPath $buildPath -ArchiveName $RootArchiveName
}

$compileArgs = @('compile', '-C', $buildPath)
if (-not $RunTests) {
	# Meson's default target includes every native test executable. VS Code
	# builds only runtime/release products; tests remain available to explicit
	# CI or command-line -RunTests builds.
	$compileTargets = New-Object System.Collections.Generic.List[string]
	if ($Target -in @('client', 'both')) {
		$compileTargets.Add('fnql')
		foreach ($rendererName in ($rendererCsv -split ',')) {
			$compileTargets.Add("fnql_${rendererName}_x86")
		}
		$compileTargets.Add('fnql-web.pak')
	}
	if ($Target -in @('dedicated', 'both')) {
		$compileTargets.Add('fnql.ded')
	}
	if ($Archive) {
		$compileTargets.Add('FnQL-pkg.fnz')
	}
	$compileArgs += $compileTargets
}
Write-Host "==> $mesonPath $($compileArgs -join ' ')"
& $mesonPath @compileArgs
if ($LASTEXITCODE -ne 0) {
	throw 'Meson compile failed.'
}

Assert-WindowsOutputs -BuildPath $buildPath -SelectedPlatform $Platform -SelectedTarget $Target -RendererCsv $rendererCsv

if ($WithSteam) {
	Invoke-FnQLSteamBuild -Repository $SteamRepo -SelectedPlatform $Platform `
		-SelectedConfiguration $Configuration -FnQLRoot $workspaceRoot -BuildPath $buildPath
}

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
	$canonicalArchivePath = Join-Path $buildPath $canonicalRootArchiveName
	if (-not (Test-Path $canonicalArchivePath)) {
		throw "Canonical root package archive was not produced: $canonicalArchivePath"
	}
	$archivePath = Join-Path $buildPath $RootArchiveName
	if ($archivePath -ne $canonicalArchivePath) {
		Copy-Item -LiteralPath $canonicalArchivePath -Destination $archivePath -Force
	}
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
