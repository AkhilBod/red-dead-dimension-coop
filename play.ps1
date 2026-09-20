# Windows launcher: build the Unreal module, launch Iron Horse, then run tracking.
[CmdletBinding()]
param(
    [string]$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8',
    [int]$Camera = 0,
    [string]$Arduino = 'COM11',
    [ValidateRange(1, 120)]
    [int]$CameraFps = 30,
    [switch]$MouseOnly,
    [switch]$LowLoad,
    [switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
$project = Join-Path $PSScriptRoot 'unreal\FingerGunGame\FingerGunGame.uproject'
$editor = Join-Path $EngineRoot 'Engine\Binaries\Win64\UnrealEditor.exe'
$build = Join-Path $EngineRoot 'Engine\Build\BatchFiles\Build.bat'
$tracker = Join-Path $PSScriptRoot 'tracker'
$python = Join-Path $tracker '.venv\Scripts\python.exe'

$problems = @()
foreach ($required in @($editor, $build, (Join-Path $EngineRoot 'Engine\Source'), (Join-Path $EngineRoot 'Engine\Plugins'))) {
    if (-not (Test-Path -LiteralPath $required)) {
        $problems += "Missing Unreal Engine file: $required"
    }
}
if (-not (Test-Path -LiteralPath $project)) { $problems += "Missing project: $project" }
if (-not $MouseOnly -and -not (Test-Path -LiteralPath $python)) {
    $problems += 'Tracker environment missing: tracker\.venv\Scripts\python.exe'
}
if ($problems.Count) {
    $problems | ForEach-Object { Write-Host $_ -ForegroundColor Yellow }
    throw 'Setup is incomplete.'
}
if ($CheckOnly) {
    Write-Host 'Launcher prerequisites found.'
    return
}
if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) {
    throw 'Close Unreal Editor before launching so the game module can be rebuilt.'
}

& $build FingerGunGameEditor Win64 Development "-Project=$project" -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw "Unreal build failed with exit code $LASTEXITCODE." }

$gameArguments = @('"' + $project + '"', '/Game/Levels/IronHorse', '-game', '-windowed', '-ResX=1600', '-ResY=900')
if ($LowLoad) {
    $gameArguments = @('"' + $project + '"', '/Game/Levels/IronHorse', '-game', '-windowed', '-ResX=1280', '-ResY=720', '-ExecCmds="t.MaxFPS 30"')
}
$game = Start-Process -FilePath $editor -ArgumentList $gameArguments -PassThru
Write-Host "Game launched (PID $($game.Id))."
if ($MouseOnly) { return }

Push-Location $tracker
try {
    $trackerArguments = @('-u', 'run.py', '--no-window', '--no-preview', '--native-camera', '--camera', $Camera, '--arduino', $Arduino, '--fps', $CameraFps)
    if ($LowLoad) { $trackerArguments += @('--pose-every', '2') }
    & $python @trackerArguments
    if ($LASTEXITCODE -ne 0) { throw "Tracker exited with code $LASTEXITCODE." }
} finally {
    Pop-Location
}
