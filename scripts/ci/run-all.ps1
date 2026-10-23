[CmdletBinding()]
param(
  [ValidateSet('Full','Quick')][string]$Mode = 'Full',
  [ValidateSet('infrastructure','native','wasm','controller','gui','smoke','e2e')][string[]]$Areas = @('infrastructure'),
  [string]$WindowsRepository = '',
  [string]$WslDistribution = 'Ubuntu-24.04',
  [string]$LinuxRepository = '',
  [string]$LinuxNodeVersion = '24.10.0',
  [string]$EmsdkRoot = '',
  [string]$ReportRoot = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (!$WindowsRepository) { $WindowsRepository = Join-Path $PSScriptRoot '../..' }
$ciRepo = (Resolve-Path -LiteralPath $WindowsRepository).Path
$ciSuite = Join-Path $PSScriptRoot 'suite.mjs'
$ciRunId = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
if (!$ReportRoot) { $ReportRoot = Join-Path $ciRepo 'work/ci' }
$ciReportDir = Join-Path ([IO.Path]::GetFullPath($ReportRoot)) $ciRunId
& git -C $ciRepo check-ignore -- $ciReportDir | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'ReportRoot must be inside a Git-ignored directory; nothing was generated.' }
$null = New-Item -ItemType Directory -Force -Path $ciReportDir
$ciStarted = (Get-Date).ToUniversalTime().ToString('o')
$ciMode = $Mode.ToLowerInvariant()
$ciAreas = $Areas -join ','
$ciNode = ''
$ciKnownCommit = 'unavailable'
$ciInvocations = New-Object 'System.Collections.Generic.List[object]'
$ciWorkerExits = @{}

function Save-CiWorkerExit([string]$Platform, [int]$Code) {
  $ciWorkerExits[$Platform] = $Code
  $ciEvidence = @{schemaVersion=1;exits=$ciWorkerExits;updatedAt=(Get-Date).ToUniversalTime().ToString('o')}
  [IO.File]::WriteAllText((Join-Path $ciReportDir 'worker-exits.json'),($ciEvidence | ConvertTo-Json -Depth 10),(New-Object Text.UTF8Encoding($false)))
}

function Invoke-Ci([string]$Program, [string[]]$Arguments) {
  # Do not let Windows PowerShell's treatment of native stderr replace the
  # actual native exit code. Workers write their own complete command logs.
  $ciOldPreference = $ErrorActionPreference
  $ciInvocation = @{command=@($Program)+$Arguments;startedAt=(Get-Date).ToUniversalTime().ToString('o');log=(Join-Path $ciReportDir ('coordinator-' + $ciInvocations.Count + '.log'))}
  [IO.File]::WriteAllText($ciInvocation.log,("command=" + (ConvertTo-Json -InputObject $ciInvocation.command -Compress) + "`r`ncwd=$ciRepo`r`n"),[Text.Encoding]::Unicode)
  $ciTimer = [Diagnostics.Stopwatch]::StartNew()
  $ciCode = 2
  try {
    $ErrorActionPreference = 'Continue'
    & $Program @Arguments 2>&1 | Tee-Object -FilePath $ciInvocation.log -Append | Out-Host
    $ciCode = $LASTEXITCODE
    return $ciCode
  } finally {
    $ErrorActionPreference = $ciOldPreference
    $ciInvocation.exitCode = $ciCode
    $ciInvocation.status = if ($ciCode -eq 0) { 'passed' } else { 'failed' }
    $ciInvocation.durationMs = $ciTimer.ElapsedMilliseconds
    $ciInvocation.finishedAt = (Get-Date).ToUniversalTime().ToString('o')
    $ciInvocations.Add($ciInvocation)
    [IO.File]::WriteAllText((Join-Path $ciReportDir 'coordinator.json'),(ConvertTo-Json -InputObject @($ciInvocations.ToArray()) -Depth 20),(New-Object Text.UTF8Encoding($false)))
  }
}
function Initialize-CiMsvc {
  if ((Get-Command cl.exe -ErrorAction SilentlyContinue) -and $env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return }
  $ciVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
  if (!(Test-Path -LiteralPath $ciVswhere)) { throw 'vswhere.exe unavailable; use an x64 Visual Studio developer PowerShell.' }
  $ciVsPath = & $ciVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (!$ciVsPath) { throw 'Visual Studio x64 C++ tools are not installed.' }
  Import-Module (Join-Path $ciVsPath 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
  Enter-VsDevShell -VsInstallPath $ciVsPath -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Host
  if (!(Get-Command cl.exe -ErrorAction SilentlyContinue) -or $env:VSCMD_ARG_TGT_ARCH -ne 'x64') { throw 'MSVC x64 initialization failed.' }
}
function Write-CiBlocked([string]$Platform, [string]$Reason) {
  $ciPath = Join-Path $ciReportDir "$Platform/report.json"
  $null = New-Item -ItemType Directory -Force -Path (Split-Path $ciPath)
  $ciData = @{schemaVersion=1;platform=$Platform;mode=$ciMode;commit=$ciKnownCommit;startedAt=$ciStarted;finishedAt=(Get-Date).ToUniversalTime().ToString('o');checksVerdict='blocked';reason=$Reason;steps=@()}
  [IO.File]::WriteAllText($ciPath,($ciData | ConvertTo-Json -Depth 20),(New-Object Text.UTF8Encoding($false)))
}

Push-Location $ciRepo
try {
  # Results must be ignored; do not generate tracked/source-tree report files.
  & git check-ignore -- $ciReportDir | Out-Null
  if ($LASTEXITCODE -ne 0) { throw 'ReportRoot must be inside a Git-ignored directory.' }
  $ciKnownCommit = (& git rev-parse HEAD).Trim()
  $ciNodeCandidate = (Get-Command node.exe -ErrorAction Stop).Source
  if ((& $ciNodeCandidate --version) -notmatch '^v24\.') { throw 'Windows Node 24 is required.' }
  $ciNode = $ciNodeCandidate
  $ciWorker = Join-Path $PSScriptRoot 'run-linux.sh'
  $ciWorkerLinux = (& wsl.exe -d $WslDistribution --exec wslpath -a -u $ciWorker).Trim()
  if ($LASTEXITCODE -ne 0) { throw 'WSL distribution or Windows mount is unavailable.' }
  $ciLinuxReportRoot = (& wsl.exe -d $WslDistribution --exec wslpath -a -u $ciReportDir).Trim()
  $ciLinuxHome = (& wsl.exe -d $WslDistribution --exec printenv HOME).Trim()
  if (!$LinuxRepository) { $LinuxRepository = "$ciLinuxHome/chess-engine-lab" }
  if (!$EmsdkRoot) { $EmsdkRoot = "$ciLinuxHome/toolchains/emsdk" }
  $ciLinuxBase = @('-d',$WslDistribution,'--exec','bash','--noprofile','--norc',$ciWorkerLinux,'--repo',$LinuxRepository,'--emsdk',$EmsdkRoot,'--node-version',$LinuxNodeVersion,'--mode',$ciMode,'--areas',$ciAreas)
  $ciWinIdentity = Join-Path $ciReportDir 'windows-source.json'
  $ciLinuxIdentity = Join-Path $ciReportDir 'linux-source.json'
  $ciInspectWin = Invoke-Ci $ciNode @($ciSuite,'inspect','--repo',$ciRepo,'--report',$ciWinIdentity)
  $ciInspectLinux = Invoke-Ci 'wsl.exe' ($ciLinuxBase + @('--action','inspect','--report',"$ciLinuxReportRoot/linux-source.json"))
  if ($ciInspectWin -ne 0 -or $ciInspectLinux -ne 0) { throw 'Source/toolchain preflight failed; inspect console errors. No repository was synchronized.' }
  $ciWinSource = Get-Content -Raw -LiteralPath $ciWinIdentity | ConvertFrom-Json
  $ciLinuxSource = Get-Content -Raw -LiteralPath $ciLinuxIdentity | ConvertFrom-Json
  if ($ciWinSource.commit -ne $ciLinuxSource.commit) {
    throw "Checkout SHA mismatch: Windows $($ciWinSource.commit), Linux $($ciLinuxSource.commit). Align them explicitly; no pull/reset/sync was attempted."
  }
  try { Initialize-CiMsvc } catch { Write-Warning "Windows native environment: $($_.Exception.Message)" }
  $ciWinExit = Invoke-Ci $ciNode @($ciSuite,'run','--repo',$ciRepo,'--platform','windows','--report',(Join-Path $ciReportDir 'windows/report.json'),'--mode',$ciMode,'--areas',$ciAreas)
  Save-CiWorkerExit 'windows' $ciWinExit
  if (!(Test-Path (Join-Path $ciReportDir 'windows/report.json'))) { Write-CiBlocked 'windows' "Worker failed before reporting (exit $ciWinExit)." }
  $ciLinuxExit = Invoke-Ci 'wsl.exe' ($ciLinuxBase + @('--action','run','--report',"$ciLinuxReportRoot/linux/report.json"))
  Save-CiWorkerExit 'linux' $ciLinuxExit
  if (!(Test-Path (Join-Path $ciReportDir 'linux/report.json'))) { Write-CiBlocked 'linux' "Worker failed before reporting (exit $ciLinuxExit). Check nvm, SDK and distro setup." }
  # Inspect both repositories again after both workers, including the checkout
  # that finished first. A later edit/commit must not receive a combined pass.
  $ciFinalWin = Invoke-Ci $ciNode @($ciSuite,'inspect','--repo',$ciRepo,'--report',(Join-Path $ciReportDir 'windows-final-source.json'))
  $ciFinalLinux = Invoke-Ci 'wsl.exe' ($ciLinuxBase + @('--action','inspect','--report',"$ciLinuxReportRoot/linux-final-source.json"))
  if ($ciFinalWin -ne 0 -or $ciFinalLinux -ne 0) { throw 'Final source inspection failed; combined acceptance is blocked.' }
} catch {
  $ciReason = $_.Exception.Message
  Write-Warning $ciReason
  [IO.File]::WriteAllText((Join-Path $ciReportDir 'coordinator-error.json'),(@{reason=$ciReason} | ConvertTo-Json),(New-Object Text.UTF8Encoding($false)))
  foreach ($ciPlatform in @('windows','linux')) { if (!(Test-Path (Join-Path $ciReportDir "$ciPlatform/report.json"))) { Write-CiBlocked $ciPlatform $ciReason } }
} finally { Pop-Location }

if ($ciNode) {
  $ciExit = Invoke-Ci $ciNode @($ciSuite,'combine','--report',$ciReportDir,'--mode',$ciMode)
} else {
  $ciExit = 2
  $ciFallback = @{schemaVersion=1;mode=$ciMode;commit=$ciKnownCommit;verdict='blocked';authoritative=$false;reason='Node 24 bootstrap unavailable; see platform reports';startedAt=$ciStarted;finishedAt=(Get-Date).ToUniversalTime().ToString('o')}
  [IO.File]::WriteAllText((Join-Path $ciReportDir 'report.json'),($ciFallback | ConvertTo-Json -Depth 20),(New-Object Text.UTF8Encoding($false)))
  'Local CI BLOCKED: Node 24 bootstrap unavailable.' | Set-Content -LiteralPath (Join-Path $ciReportDir 'report.md') -Encoding UTF8
}
Write-Host "Local CI evidence: $ciReportDir"
exit $ciExit
