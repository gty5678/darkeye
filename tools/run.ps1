param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$DataDirectory = "",
    [string]$ConfigDirectory = "",
    [string]$AvwikiDirectory = ""
)

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$ConfigurationName = $Configuration.ToLowerInvariant()
$BuildDirectory = Join-Path $ProjectRoot "build\windows-msvc-$ConfigurationName"
$Executable = Join-Path $BuildDirectory "Darkeye.exe"
$QtBin = "C:\Qt\6.12.0\msvc2022_64\bin"
$CMakeCache = Join-Path $BuildDirectory "CMakeCache.txt"
if (Test-Path -LiteralPath $CMakeCache) {
    $QtDirectoryEntry = Get-Content -LiteralPath $CMakeCache |
        Select-String '^Qt6_DIR:[^=]+=(.+)$' | Select-Object -First 1
    if ($QtDirectoryEntry) {
        $QtBin = [IO.Path]::GetFullPath((Join-Path $QtDirectoryEntry.Matches[0].Groups[1].Value "../../../bin"))
    }
}

if (-not (Test-Path -LiteralPath $Executable)) {
    throw "程序尚未编译，请先运行 python .\tools\build.py --config $Configuration"
}

$env:PATH = "$QtBin;$env:PATH"
$LaunchArguments = @()
foreach ($DirectoryOption in @(
    @{ Name = 'config-dir'; Value = $ConfigDirectory; Default = 'config' },
    @{ Name = 'avwiki-dir'; Value = $AvwikiDirectory; Default = 'avwiki' },
    @{ Name = 'data-dir'; Value = $DataDirectory; Default = 'data' }
)) {
    $DirectoryPath = $DirectoryOption.Value
    if ([string]::IsNullOrWhiteSpace($DirectoryPath)) {
        $DirectoryPath = Join-Path $ProjectRoot $DirectoryOption.Default
    }
    $LaunchArguments += "--$($DirectoryOption.Name)"
    $LaunchArguments += [IO.Path]::GetFullPath($DirectoryPath)
}
& $Executable @LaunchArguments
