param(
    [Parameter(Mandatory = $true)]
    [string]$SourceData,
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [string]$TargetData = ""
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$SourceRoot = [IO.Path]::GetFullPath($SourceData)
if ([string]::IsNullOrWhiteSpace($TargetData)) {
    $ConfigurationName = $Configuration.ToLowerInvariant()
    $TargetData = Join-Path $ProjectRoot "build\windows-msvc-$ConfigurationName\preview-data"
}
$TargetRoot = [IO.Path]::GetFullPath($TargetData)

if ($SourceRoot -eq $TargetRoot) {
    throw "源 data 与预览 data 不能是同一目录"
}
if ($TargetRoot.StartsWith($SourceRoot + [IO.Path]::DirectorySeparatorChar,
                           [StringComparison]::OrdinalIgnoreCase)) {
    throw "预览 data 不能位于源 data 内部"
}

$SourcePublic = Join-Path $SourceRoot "public\public.db"
$SourcePrivate = Join-Path $SourceRoot "private\private.db"
foreach ($Database in @($SourcePublic, $SourcePrivate)) {
    if (-not (Test-Path -LiteralPath $Database -PathType Leaf)) {
        throw "找不到源数据库：$Database"
    }
}

$Sqlite = (Get-Command sqlite3 -ErrorAction SilentlyContinue).Source
if (-not $Sqlite) {
    throw "找不到 sqlite3，无法创建运行中数据库的一致性快照"
}

$Stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$ArchiveRoot = Join-Path $TargetRoot "preview-backup\$Stamp"
foreach ($RelativeDatabase in @("public\public.db", "private\private.db")) {
    $Existing = Join-Path $TargetRoot $RelativeDatabase
    if (Test-Path -LiteralPath $Existing -PathType Leaf) {
        $Archived = Join-Path $ArchiveRoot $RelativeDatabase
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Archived) | Out-Null
        Move-Item -LiteralPath $Existing -Destination $Archived
    }
}

foreach ($Pair in @(
    @($SourcePublic, (Join-Path $TargetRoot "public\public.db")),
    @($SourcePrivate, (Join-Path $TargetRoot "private\private.db"))
)) {
    $SourceDatabase = $Pair[0]
    $TargetDatabase = $Pair[1]
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $TargetDatabase) | Out-Null
    $TemporaryDatabase = "$TargetDatabase.tmp"
    if (Test-Path -LiteralPath $TemporaryDatabase) {
        Remove-Item -LiteralPath $TemporaryDatabase
    }
    & $Sqlite -readonly $SourceDatabase ".backup '$TemporaryDatabase'"
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $TemporaryDatabase)) {
        throw "数据库快照失败：$SourceDatabase"
    }
    & $Sqlite -readonly $TemporaryDatabase "PRAGMA quick_check;"
    if ($LASTEXITCODE -ne 0) {
        throw "数据库快照校验失败：$TemporaryDatabase"
    }
    Move-Item -LiteralPath $TemporaryDatabase -Destination $TargetDatabase
}

# 只带入影响视觉与页面状态的设置，绝不复制数据库、媒体、采集器或凭据路径。
# 这样预览会复现 Python 版的主题/作品页布局，同时仍只读写独立快照目录。
$SourceSettings = Join-Path $SourceRoot "settings.ini"
if (Test-Path -LiteralPath $SourceSettings -PathType Leaf) {
    $AllowedSections = @("App", "WorkPage", "ShelfPage")
    $PreviewSettings = New-Object System.Collections.Generic.List[string]
    $IncludeSection = $false
    foreach ($Line in [IO.File]::ReadAllLines($SourceSettings)) {
        if ($Line -match '^\[([^]]+)\]\s*$') {
            $IncludeSection = $AllowedSections -contains $Matches[1]
            if ($IncludeSection) {
                if ($PreviewSettings.Count -gt 0) { $PreviewSettings.Add("") }
                $PreviewSettings.Add($Line)
            }
            continue
        }
        if ($IncludeSection) { $PreviewSettings.Add($Line) }
    }
    [IO.File]::WriteAllLines(
        (Join-Path $TargetRoot "settings.ini"),
        $PreviewSettings,
        [Text.UTF8Encoding]::new($false)
    )
}

foreach ($DirectoryName in @("workcovers", "fanart", "actressimages", "actorimages")) {
    $SourceDirectory = Join-Path $SourceRoot "public\$DirectoryName"
    $TargetDirectory = Join-Path $TargetRoot "public\$DirectoryName"
    if (Test-Path -LiteralPath $SourceDirectory -PathType Container) {
        New-Item -ItemType Directory -Force -Path $TargetDirectory | Out-Null
        & robocopy $SourceDirectory $TargetDirectory /E /XO /R:1 /W:1 /NFL /NDL /NJH /NJS /NP
        if ($LASTEXITCODE -ge 8) {
            throw "资源快照失败：$SourceDirectory"
        }
    }
}

Write-Host "预览数据已准备：$TargetRoot"
Write-Host ".\tools\run.ps1 -Configuration $Configuration -DataDirectory `"$TargetRoot`""
