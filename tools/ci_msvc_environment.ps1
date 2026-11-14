# Ninja needs the native MSVC environment that Visual Studio generators select
# themselves. Persist only changes made by VsDevShell into later Actions steps.
param([Parameter(Mandatory)][ValidateSet('x64', 'arm64')][string]$Architecture)
$Architecture = $Architecture.ToLowerInvariant()
$ErrorActionPreference = 'Stop'
$before = @{}
Get-ChildItem Env: | ForEach-Object { $before[$_.Name] = $_.Value }
# Hosted images carry English MSVC resources; keep /showIncludes consistent.
$env:VSLANG = '1033'
$component = if ($Architecture -eq 'arm64') { 'ARM64' } else { 'x86.x64' }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires "Microsoft.VisualStudio.Component.VC.Tools.$component" -property installationPath
if (!$vs) { throw "Native $Architecture MSVC tools were not found" }
Import-Module "$vs/Common7/Tools/Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation `
    -DevCmdArguments "-arch=$Architecture -host_arch=$Architecture"
if ($env:GITHUB_ENV) {
    Get-ChildItem Env: | Where-Object { $before[$_.Name] -ne $_.Value } |
        ForEach-Object { "$($_.Name)=$($_.Value)" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append }
}
if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) { throw 'cl.exe is missing from PATH' }
