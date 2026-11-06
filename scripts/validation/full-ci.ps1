# CELab default local Full CI: one deterministic execution; no Codex orchestration.
[CmdletBinding()]
param(
    [switch]$PlanOnly,
    [string]$ReferenceRoot,
    [string]$WslDistribution = 'Ubuntu-24.04'
)
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'workflow.py'
if (!(Test-Path -LiteralPath $script)) { throw 'CELab validation workflow.py not found' }
$argsForWorkflow = @('--distribution', $WslDistribution)
if ($PlanOnly) { $argsForWorkflow += '--plan-only' }
if ($ReferenceRoot) { $argsForWorkflow += @('--reference-root', $ReferenceRoot) }
& python -u $script @argsForWorkflow
exit $LASTEXITCODE
