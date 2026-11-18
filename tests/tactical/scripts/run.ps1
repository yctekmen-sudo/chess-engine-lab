[CmdletBinding()]
param(
    [string]$RunRoot = '',
    [ValidateRange(1,12)][int]$Depth = 5,
    [ValidateRange(1,5)][int]$Repetitions = 1,
    [ValidateRange(0,204)][int]$Limit = 0,
    [string]$ArasanPath = '',
    [string]$WslDistribution = 'Ubuntu-24.04',
    [switch]$SkipWsl
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$kit = Split-Path -Parent $MyInvocation.MyCommand.Path
$tag = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8)
if (!$RunRoot) { $RunRoot = Join-Path 'C:\cursor' ('celab-s0-' + $tag) }
if (Test-Path -LiteralPath $RunRoot) { throw 'RunRoot already exists. Refusing to modify an existing experiment.' }
$null = New-Item -Path $RunRoot -ItemType Directory
$evidence = Join-Path $RunRoot 'evidence'
$null = New-Item -Path $evidence -ItemType Directory
$work = Join-Path $RunRoot 'windows'
$zip = Join-Path $env:USERPROFILE ('Downloads\CELab-S0-baseline-evidence-' + $tag + '.zip')
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
    if ($code -ne 0) { throw "Step $Name failed (exit $code). Log: $file" }
}
function WslPath([string]$p) {
    $value = & wsl.exe -d $WslDistribution --exec wslpath -a -u $p
    if ($LASTEXITCODE -ne 0) { throw "Failed to resolve WSL path: $p" }
    return ($value | Select-Object -First 1).Trim()
}
function Verify-Node24 {
    $value = (& node.exe --version)
    if ($LASTEXITCODE -ne 0 -or $value -notmatch '^v24\.') { throw 'Windows Node.js 24 is required' }
}
function Initialize-Msvc {
    if ((Get-Command cl.exe -ErrorAction SilentlyContinue) -and $env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio vswhere.exe missing' }
    $install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or !$install) { throw 'Visual Studio C++ x64 tools missing' }
    Import-Module (Join-Path ($install | Select-Object -Last 1) 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath ($install | Select-Object -Last 1) -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Host
    if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'MSVC initialization failed' }
}
try {
    $run = [ordered]@{schema=1;status='incomplete';base='f92a170deee6d9e56b173101ad50aca6c619ab6d';depth=$Depth;
        repetitions=$Repetitions;limit=$Limit;skipWsl=[bool]$SkipWsl;runRoot=$RunRoot;startedUTC=(Get-Date).ToUniversalTime().ToString('o')}
    ($run | ConvertTo-Json -Depth 10) | Set-Content -LiteralPath (Join-Path $evidence 'run.json') -Encoding UTF8
    $arasan = Join-Path $evidence 'arasan2026.epd'
    if ($ArasanPath) {
        Copy-Item -LiteralPath $ArasanPath -Destination $arasan -ErrorAction Stop
    } else {
        # Version-controlled source; never silently update during validation.
        Copy-Item -LiteralPath (Join-Path $kit '..\corpus\arasan2026.epd') -Destination $arasan -ErrorAction Stop
    }
    $blob = (& git hash-object -- $arasan).Trim()
    if ($LASTEXITCODE -ne 0 -or $blob -ne '29fa318d960c73dc511eb4589c32ae9595f4b9ba') {
        throw "Arasan EPD source identity mismatch. Expected blob 29fa318d..., observed $blob"
    }
    Write-Host "Dataset blob matched pinned upstream Git object: $blob"
    Verify-Node24
    Step 'clone-windows' 'git.exe' @('clone','--no-checkout','--depth','100','https://github.com/yctekmen-sudo/chess-engine-lab.git',$work)
    Step 'checkout-windows' 'git.exe' @('-C',$work,'checkout','--detach','f92a170deee6d9e56b173101ad50aca6c619ab6d')
    $sha = (& git.exe -C $work rev-parse HEAD).Trim()
    $tree = (& git.exe -C $work rev-parse 'HEAD^{tree}').Trim()
    if ($sha -ne 'f92a170deee6d9e56b173101ad50aca6c619ab6d') { throw 'Wrong Windows baseline commit' }
    if (git.exe -C $work status --porcelain --untracked-files=all) { throw 'Windows source checkout not clean' }
    $identity = @{sha=$sha;tree=$tree;source='origin/main at pinned merged commit'}
    ($identity | ConvertTo-Json) | Set-Content -LiteralPath (Join-Path $evidence 'windows-identity.json') -Encoding UTF8
    Initialize-Msvc
    $build = Join-Path $work 'build\native-release'
    Step 'configure-windows' 'cmake.exe' @('-S',$work,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=ON')
    Step 'build-windows' 'cmake.exe' @('--build',$build,'--parallel','2')
    Step 'npm-windows' 'npm.cmd' @('--prefix',(Join-Path $work 'controller'),'ci','--ignore-scripts')
    Step 'ctest-windows' 'ctest.exe' @('--test-dir',$build,'--output-on-failure')
    $corpus = Join-Path $evidence 'frozen-corpus.json'
    Step 'freeze-corpus' 'node.exe' @((Join-Path $kit 'freeze.mjs'),'--repo',$work,'--epd',$arasan,
        '--csv',(Join-Path $kit '..\corpus\lichess-official-samples.csv'),'--out',$corpus)
    $engine = Join-Path $build 'chess_lab_uci.exe'
    if (!(Test-Path -LiteralPath $engine)) { throw "Windows UCI executable missing: $engine" }
    $benchArgs = @((Join-Path $kit 'bench.mjs'),'--engine',$engine,'--corpus',$corpus,'--out',(Join-Path $evidence 'windows.jsonl'),
        '--platform','windows','--depth',"$Depth",'--repetitions',"$Repetitions")
    if ($Limit -ne 0) { $benchArgs += @('--limit',"$Limit") }
    Step 'benchmark-windows' 'node.exe' $benchArgs
    if (!$SkipWsl) {
        $kitWsl = WslPath $kit
        $srcWsl = WslPath $work
        $evWsl = WslPath $evidence
        Step 'benchmark-linux' 'wsl.exe' @('-d',$WslDistribution,'--exec','bash',"$kitWsl/linux.sh",$srcWsl,$evWsl,$kitWsl,$tag,"$Depth","$Repetitions","$Limit")
        $linuxIdentity = Get-Content -Raw -LiteralPath (Join-Path $evidence 'linux-identity.json') | ConvertFrom-Json
        if ($linuxIdentity.sha -ne $sha -or $linuxIdentity.tree -ne $tree -or $linuxIdentity.trackedChanges) {
            throw 'Linux baseline identity/source check failed'
        }
    }
    $python = (Get-Command python.exe -ErrorAction Stop).Source
    $analysisArgs = @((Join-Path $kit 'analyze.py'),'--evidence',$evidence,'--depth',"$Depth",'--reps',"$Repetitions")
    if ($Limit -ne 0) { $analysisArgs += @('--limit',"$Limit") }
    if ($SkipWsl) { $analysisArgs += '--skip-wsl' }
    Step 'analysis' $python $analysisArgs
    $runStatus = 'complete'
} catch {
    $runStatus = 'failed'
    $message = ($_ | Out-String)
    Write-Warning $message
    Set-Content -LiteralPath (Join-Path $evidence 'failure.txt') -Value $message -Encoding UTF8
} finally {
    $state = [ordered]@{status=$runStatus;finishedUTC=(Get-Date).ToUniversalTime().ToString('o');
      base='f92a170deee6d9e56b173101ad50aca6c619ab6d';depth=$Depth;repetitions=$Repetitions;limit=$Limit}
    ($state | ConvertTo-Json) | Set-Content -LiteralPath (Join-Path $evidence 'final-status.json') -Encoding UTF8
    Write-Host "`n=== Sealing evidence ===" -ForegroundColor Cyan
    $python = (Get-Command python.exe -ErrorAction SilentlyContinue)
    if ($python) {
       & $python.Source (Join-Path $kit 'seal.py') $evidence --out $zip
       if ($LASTEXITCODE -ne 0) { Write-Warning "Evidence sealing failed. Partial evidence at $evidence" }
    } else { Write-Warning "Python unavailable; partial evidence at $evidence" }
    Write-Host "EVIDENCE_PATH: $zip"
    Write-Host "RUN_STATE: $runStatus"
}
if ($runStatus -ne 'complete') { throw "CELab S0 baseline incomplete; please upload the evidence ZIP: $zip" }
