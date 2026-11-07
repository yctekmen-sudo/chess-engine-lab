# CELab C1 local Windows development checks. Never launches production Full CI.
[CmdletBinding()]
param(
    [switch]$Execute,
    [string]$BaseRef = 'HEAD',
    [string]$Areas = ''
)
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'focused.py'
if (!(Test-Path -LiteralPath $script)) { throw 'C1 focused.py missing' }

# Existing Quick worker requires x64 MSVC for native Debug dependencies.
# Install the same Visual Studio developer shell that run-all.ps1 uses.
if ($Execute -and (!(Get-Command cl.exe -ErrorAction SilentlyContinue) -or
                    $env:VSCMD_ARG_TGT_ARCH -ne 'x64')) {
    try {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (Test-Path -LiteralPath $vswhere) {
            $install = & $vswhere -latest -products '*' -requires `
                'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' -property installationPath
            if ($install) {
                Import-Module (Join-Path $install 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
                Enter-VsDevShell -VsInstallPath $install -SkipAutomaticLocation `
                    -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Host
            }
        }
    } catch {
        Write-Warning "MSVC developer shell unavailable: $($_.Exception.Message)"
    }
    # If MSVC remains unavailable, the existing worker will explicitly block
    # its native gates. Never mark them skipped or passed.
}
$argList = @('-u', $script, '--base-ref', $BaseRef)
if ($Execute) { $argList += '--execute' }
if ($Areas) { $argList += @('--areas', $Areas) }
& python @argList
exit $LASTEXITCODE
