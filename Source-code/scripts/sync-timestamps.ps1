# v0.5.11 (Build-Windows.cmd step 1b): recompile by CONTENT, not by date.
# The package keeps each file's original date so a quick build only recompiles
# what changed. But a changed file can carry a date OLDER than the objects of an
# earlier build (e.g. a replaced package with the same version), and Visual
# Studio would then keep the old objects: parts of the plugin compiled from
# different source versions, which crashes. This compares every source file's
# SHA-256 with the list saved by the last successful build and gives each
# changed (or new) file the current date, so it is always recompiled.
param([Parameter(Mandatory=$true)][string]$Src,[Parameter(Mandatory=$true)][string]$State)
$ErrorActionPreference='Stop'
$Src=(Resolve-Path -LiteralPath $Src).Path.TrimEnd('\')
$files=@{}
foreach($root in @('Source','Tests','scripts','ThirdParty\r8brain')){
    $dir=Join-Path $Src $root
    if(Test-Path -LiteralPath $dir){
        Get-ChildItem -LiteralPath $dir -Recurse -File | ForEach-Object { $files[$_.FullName.Substring($Src.Length+1)]=$_.FullName }
    }
}
foreach($single in @('CMakeLists.txt')){ $p=Join-Path $Src $single; if(Test-Path -LiteralPath $p){ $files[$single]=$p } }
$old=@{}
if(Test-Path -LiteralPath $State){
    Get-Content -LiteralPath $State | ForEach-Object { $i=$_.IndexOf(' '); if($i -gt 0){ $old[$_.Substring($i+1)]=$_.Substring(0,$i) } }
}
$now=Get-Date; $changed=0; $headerChanged=$false; $lines=New-Object System.Collections.Generic.List[string]
foreach($key in ($files.Keys | Sort-Object)){
    $hash=(Get-FileHash -Algorithm SHA256 -LiteralPath $files[$key]).Hash
    $lines.Add("$hash $key")
    if($old[$key] -ne $hash){
        (Get-Item -LiteralPath $files[$key]).LastWriteTime=$now; $changed++
        if($key -notmatch '\.cpp$' -and $key -ne 'CMakeLists.txt'){ $headerChanged=$true }
    }
}
# v0.5.16: Visual Studio does NOT track headers that live under %LOCALAPPDATA%
# (MSBuild's FileTracker skips the AppData folders), and the mirror is there.
# A changed header therefore never recompiled the .cpp files that include it:
# stale objects with an old PunchTailProcessor layout (heap corruption, the
# v0.5.11 crash). When any header changed, every Tailtone .cpp is recompiled.
if($headerChanged -and (Test-Path -LiteralPath $State)){
    $cpp=@($files.Keys | Where-Object { $_ -match '\.cpp$' -and $_ -match '^(Source|Tests)\\' })
    foreach($key in $cpp){ (Get-Item -LiteralPath $files[$key]).LastWriteTime=$now }
    Write-Host "A header changed: all $($cpp.Count) Tailtone .cpp files will be recompiled (headers in AppData are not tracked by Visual Studio)"
}
Set-Content -LiteralPath ($State+'.pending') -Value $lines -Encoding ASCII
if(Test-Path -LiteralPath $State){ Write-Host "Source files changed since the last build: $changed (they will be recompiled)" }
else { Write-Host "No record of the last build: all $changed Tailtone source files will be recompiled once" }
