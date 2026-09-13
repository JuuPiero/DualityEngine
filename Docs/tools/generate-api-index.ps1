<#
    Builds the static data consumed by pages/api-reference.html.

    Run from the repository root:
      powershell -ExecutionPolicy Bypass -File Docs/tools/generate-api-index.ps1

    The generated index deliberately embeds the public headers themselves. The
    browser can therefore search and display the exact declarations used by the
    current engine without needing a web server or a C++ parser at runtime.
#>

param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'
$includeRoot = Join-Path $RepositoryRoot 'DualityEngine\Include\DualityEngine'
$outputPath = Join-Path $RepositoryRoot 'Docs\js\api-index.js'

function Get-DeclaredSymbols {
    param([string]$Source)

    $symbols = [System.Collections.Generic.List[object]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)

    foreach ($match in [regex]::Matches($Source, '(?m)^\s*(?:class|struct|enum(?:\s+class)?)\s+([A-Za-z_]\w*)')) {
        $name = $match.Groups[1].Value
        if ($seen.Add("type:$name")) {
            $symbols.Add([ordered]@{ kind = 'type'; name = $name })
        }
    }

    # Public headers use one-line declarations or one-line inline definitions for
    # almost every callable API. This intentionally indexes only signature-shaped
    # lines; the full header remains available in `source` for declarations split
    # over several lines.
    foreach ($line in ($Source -split "`r?`n")) {
        if ($line -match '^\s*(?:if|for|while|switch|return)\b') { continue }
        if ($line -notmatch '\(') { continue }
        if ($line -notmatch '(?:;|\{)\s*(?://.*)?$') { continue }
        if ($line -match '\b(?:operator|static_assert)\s*\(') { continue }
        if ($line -match '(?<![:\w])(~?[A-Za-z_]\w*)\s*\(') {
            $name = $Matches[1]
            if ($seen.Add("function:$name")) {
                $symbols.Add([ordered]@{ kind = 'function'; name = $name })
            }
        }
    }

    return @($symbols | Sort-Object kind, name)
}

$records = foreach ($file in Get-ChildItem -Path $includeRoot -Filter '*.h' -Recurse | Sort-Object FullName) {
    $relativePath = $file.FullName.Substring($includeRoot.Length + 1).Replace('\', '/')
    $source = [System.IO.File]::ReadAllText($file.FullName).Replace("`r`n", "`n")
    [ordered]@{
        path = "DualityEngine/Include/DualityEngine/$relativePath"
        module = ($relativePath -split '/')[0]
        name = $file.BaseName
        symbols = @(Get-DeclaredSymbols $source)
        source = $source
    }
}

$json = $records | ConvertTo-Json -Depth 6 -Compress
$banner = "// Generated from DualityEngine/Include. Do not hand-edit; run Docs/tools/generate-api-index.ps1.`n"
[System.IO.File]::WriteAllText($outputPath, "$banner`nwindow.DualityApiIndex = $json;`n", [System.Text.UTF8Encoding]::new($false))
Write-Host "Generated $outputPath with $($records.Count) public headers."
