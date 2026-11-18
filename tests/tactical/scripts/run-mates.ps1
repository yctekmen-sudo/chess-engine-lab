[CmdletBinding()]
param(
    [string]$RunRoot = '',
    [ValidateRange(1,16)][int]$Depth = 5,
    [ValidateRange(1,5)][int]$Repetitions = 1,
    [ValidateRange(1,8)][int]$MateFrom = 1,
    [ValidateRange(1,8)][int]$MateTo = 8,
    [ValidateRange(5,3600)][int]$TimeoutSeconds = 60,
    [string]$WslDistribution = 'Ubuntu-24.04',
    [switch]$SkipWsl
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($MateTo -lt $MateFrom) { throw 'MateTo cannot be less than MateFrom' }
$kit = Split-Path -Parent $MyInvocation.MyCommand.Path
$tag = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
if (!$RunRoot) { $RunRoot = Join-Path 'C:\cursor' ('celab-mates-' + $tag) }
if (Test-Path -LiteralPath $RunRoot) { throw 'RunRoot already exists; refusing to reuse an experiment' }
$null = New-Item -Path $RunRoot -ItemType Directory
$evidence = Join-Path $RunRoot 'evidence'
$null = New-Item -Path $evidence -ItemType Directory
$work = Join-Path $RunRoot 'windows'
$zip = Join-Path $env:USERPROFILE ('Downloads\CELab-S0-mates-evidence-' + $tag + '.zip')
$runStatus = 'incomplete'
function Step([string]$Name,[string]$Executable,[string[]]$Arguments) {
    $file = Join-Path $evidence ($Name + '.log')
    Write-Host "`n=== $Name ===" -ForegroundColor Cyan
    $old = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath $file | Out-Host
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $old }
    if ($code -ne 0) { throw "Step $Name failed (exit $code): $file" }
}
function WslPath([string]$p) {
    $value = & wsl.exe -d $WslDistribution --exec wslpath -a -u $p
    if ($LASTEXITCODE -ne 0) { throw "WSL path translation failed for $p" }
    return ($value | Select-Object -First 1).Trim()
}
function Verify-Node24 {
    $value = & node.exe --version
    if ($LASTEXITCODE -ne 0 -or $value -notmatch '^v24\.') { throw 'Windows Node.js 24 required' }
}
function Initialize-Msvc {
    if ((Get-Command cl.exe -ErrorAction SilentlyContinue) -and $env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio vswhere missing' }
    $install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$install) { throw 'Visual Studio C++ tools missing' }
    Import-Module (Join-Path ($install | Select-Object -Last 1) 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath ($install | Select-Object -Last 1) -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Host
    if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC initialization failed' }
}
try {
    $run = [ordered]@{schema=2;status='incomplete';kind='mate';base='f92a170deee6d9e56b173101ad50aca6c619ab6d';
       depth=$Depth;repetitions=$Repetitions;mateFrom=$MateFrom;mateTo=$MateTo;timeoutSeconds=$TimeoutSeconds;
       skipWsl=[bool]$SkipWsl;runRoot=$RunRoot;startedUTC=(Get-Date).ToUniversalTime().ToString('o')}
    ($run | ConvertTo-Json -Depth 10) | Set-Content -LiteralPath (Join-Path $evidence 'run.json') -Encoding UTF8
    $source = Join-Path $evidence 'matetrack-selected.epd'
    Copy-Item -LiteralPath (Join-Path $kit '..\corpus\matetrack-selected.epd') -Destination $source
    $expectedSelected = '79bdb4ac882aab713096b6b59a5bc07e9c3b4ba61c17f82d1ae606fd40fcdfb3'
    $observedSelected = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($observedSelected -ne $expectedSelected) { throw 'Frozen MateTrack selection hash mismatch' }
    Verify-Node24
    Step 'clone-windows' 'git.exe' @('clone','--no-checkout','--depth','100','https://github.com/yctekmen-sudo/chess-engine-lab.git',$work)
    Step 'checkout-windows' 'git.exe' @('-C',$work,'checkout','--detach','f92a170deee6d9e56b173101ad50aca6c619ab6d')
    $sha = (& git.exe -C $work rev-parse HEAD).Trim()
    $tree = (& git.exe -C $work rev-parse 'HEAD^{tree}').Trim()
    if ($sha -ne 'f92a170deee6d9e56b173101ad50aca6c619ab6d') { throw 'Wrong Windows baseline' }
    if (git.exe -C $work status --porcelain --untracked-files=all) { throw 'Windows baseline checkout not clean' }
    (@{sha=$sha;tree=$tree;source='exact PR53 merged commit'} | ConvertTo-Json) | Set-Content -LiteralPath (Join-Path $evidence 'windows-identity.json') -Encoding UTF8
    Initialize-Msvc
    $build = Join-Path $work 'build\native-release'
    Step 'configure-windows' 'cmake.exe' @('-S',$work,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON')
    Step 'build-windows' 'cmake.exe' @('--build',$build,'--parallel','2')
    Step 'npm-windows' 'npm.cmd' @('--prefix',(Join-Path $work 'controller'),'ci','--ignore-scripts')
    Step 'ctest-windows' 'ctest.exe' @('--test-dir',$build,'--output-on-failure')
    $corpus = Join-Path $evidence 'frozen-mates.json'
    Step 'freeze-mates' 'node.exe' @((Join-Path $kit 'freeze-mates.mjs'),'--repo',$work,'--source',$source,'--out',$corpus)
    $engine = Join-Path $build 'chess_lab_uci.exe'
    if (!(Test-Path -LiteralPath $engine)) { throw 'Missing Windows native UCI executable' }
    Step 'benchmark-windows' 'node.exe' @((Join-Path $kit 'bench-mates.mjs'),'--engine',$engine,'--repo',$work,
       '--corpus',$corpus,'--out',(Join-Path $evidence 'windows.jsonl'),'--platform','windows',
       '--depth',"$Depth",'--repetitions',"$Repetitions",'--mate-from',"$MateFrom",'--mate-to',"$MateTo",'--timeout-seconds',"$TimeoutSeconds")
    if (!$SkipWsl) {
        $kitWsl = WslPath $kit
        $srcWsl = WslPath $work
        $evWsl = WslPath $evidence
        Step 'benchmark-linux' 'wsl.exe' @('-d',$WslDistribution,'--exec','bash',"$kitWsl/linux-mates.sh",$srcWsl,$evWsl,$kitWsl,$tag,"$Depth","$Repetitions","$MateFrom","$MateTo","$TimeoutSeconds")
        $linuxIdentity = Get-Content -Raw -LiteralPath (Join-Path $evidence 'linux-identity.json') | ConvertFrom-Json
        if ($linuxIdentity.sha -ne $sha -or $linuxIdentity.tree -ne $tree -or $linuxIdentity.trackedChanges) { throw 'Linux source identity mismatch' }
    }
    $python = (Get-Command python.exe -ErrorAction Stop).Source
    $argsAnalysis = @((Join-Path $kit 'analyze-mates.py'),'--evidence',$evidence,'--depth',"$Depth",'--reps',"$Repetitions",'--mate-from',"$MateFrom",'--mate-to',"$MateTo")
    if ($SkipWsl) { $argsAnalysis += '--skip-wsl' }
    Write-Host "`n=== analyze-mates ===" -ForegroundColor Cyan
    $old = $ErrorActionPreference
    try {
        $ErrorActionPreference='Continue'
        & $python @argsAnalysis 2>&1 | Tee-Object -FilePath (Join-Path $evidence 'analyze-mates.log') | Out-Host
        $code=$LASTEXITCODE
    } finally { $ErrorActionPreference=$old }
    if ($code -eq 2) { $runStatus='review_required' }
    elseif ($code -eq 0) { $runStatus='complete' }
    else { throw "Mate analysis failed with exit code $code" }
} catch {
    $runStatus = 'failed'
    $message = ($_ | Out-String)
    Write-Warning $message
    Set-Content -LiteralPath (Join-Path $evidence 'failure.txt') -Value $message -Encoding UTF8
} finally {
    $state = [ordered]@{status=$runStatus;finishedUTC=(Get-Date).ToUniversalTime().ToString('o');
      base='f92a170deee6d9e56b173101ad50aca6c619ab6d';depth=$Depth;repetitions=$Repetitions;
      mateFrom=$MateFrom;mateTo=$MateTo;timeoutSeconds=$TimeoutSeconds}
    ($state | ConvertTo-Json) | Set-Content -LiteralPath (Join-Path $evidence 'final-status.json') -Encoding UTF8
    Write-Host "`n=== Sealing evidence ===" -ForegroundColor Cyan
    $python = (Get-Command python.exe -ErrorAction SilentlyContinue)
    if ($python) {
       & $python.Source (Join-Path $kit 'seal.py') $evidence --out $zip
       if ($LASTEXITCODE -ne 0) { Write-Warning "Evidence sealing failed. Partial evidence at $evidence" }
    } else { Write-Warning "Python missing. Partial evidence at $evidence" }
    Write-Host "EVIDENCE_PATH: $zip"
    Write-Host "RUN_STATE: $runStatus"
}
if ($runStatus -ne 'complete') { throw "CELab S0 mates not fully verified ($runStatus); upload evidence ZIP: $zip" }
