<#
.SYNOPSIS
    Emits paste-ready `Game::Doc` descriptor skeletons for registrations that
    do not have one yet.

.DESCRIPTION
    Every `Game::Lua::Register*` call can carry a documentation descriptor (see
    `namespace Game::Doc` in src/Game.h). Roughly 700 registrations still need
    one. This script does the mechanical part of each: it finds the registered
    names in a source file, recovers the argument list from that file's
    `Usage:` strings, recovers a summary from the function's section in
    docs/API.md, and writes a skeleton you paste into the file.

    It writes ONLY into build/docskel. It never edits src/.

    Every guess it makes is marked `// TODO`. A skeleton is a starting point,
    not an answer: the `Script_*` body is the authority on the real signature,
    and docs/API.md is stale in places. Read the body, fix the skeleton, then
    paste it.

.PARAMETER Path
    Source files to process. Accepts globs. Defaults to every .cpp under src.

.PARAMETER OutDir
    Where to write the skeletons. Defaults to build/docskel.

.EXAMPLE
    ./tools/New-ApiDocSkeletons.ps1 -Path src/item/*.cpp
    Writes build/docskel/item/<file>.skel.cpp for each item module.
#>
[CmdletBinding()]
param(
    [string[]] $Path = @('src/**/*.cpp'),
    [string] $OutDir = 'build/docskel'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
Set-Location $repo

# ---------------------------------------------------------------------------
# docs/API.md: heading -> first sentence of the paragraph under it.
# Headings look like:  ### `C_Spell.GetSpellInfo(spellID)`
# or a combined pair:  ### `IsUsableSpell(spell)` / `IsUsableSpell(slot, ...)`
# ---------------------------------------------------------------------------
function Read-ApiSummaries {
    $summaries = @{}
    $lines = Get-Content 'docs/API.md' -Encoding UTF8
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -notmatch '^###\s+(.+)$') { continue }
        $heading = $Matches[1]

        # Every `Name(...)` in the heading maps to the same summary.
        $names = [regex]::Matches($heading, '`([A-Za-z_][A-Za-z0-9_.:]*)\s*\(') |
                 ForEach-Object { $_.Groups[1].Value }
        if (-not $names) { continue }

        # First non-blank prose line after the heading, trimmed to one sentence.
        $summary = $null
        for ($j = $i + 1; $j -lt [Math]::Min($i + 8, $lines.Count); $j++) {
            $line = $lines[$j].Trim()
            if (-not $line) { continue }
            if ($line -match '^(```|\||>|#|-\s)') { break }
            $summary = $line
            break
        }
        if (-not $summary) { continue }
        if ($summary -match '^(.+?[.!?])(\s|$)') { $summary = $Matches[1] }
        $summary = $summary -replace '\[([^\]]+)\]\([^)]+\)', '$1' `
                            -replace '[`*]', '' -replace '"', '\"'

        foreach ($n in $names) {
            $short = ($n -split '\.')[-1]
            if (-not $summaries.ContainsKey($short)) { $summaries[$short] = $summary }
        }
    }
    return $summaries
}

# ---------------------------------------------------------------------------
# Argument-name -> Blizzard type guess. Anything unmatched is TODO.
# ---------------------------------------------------------------------------
function Get-TypeGuess([string] $name) {
    switch -Regex ($name) {
        '^unit'                                   { return 'UnitToken' }
        '^(spell|spellIdentifier)$'               { return 'SpellIdentifier' }
        '^(item|itemIdentifier|itemInfo)$'        { return 'ItemInfo' }
        '(ID|Id|Index|Count|Slot|Level|Amount|Duration|Rate|Time)$' { return 'number' }
        '^(index|slot|count|level|duration)$'     { return 'number' }
        '^(name|link|text|token|path|texture|locale|type|bookType)$' { return 'string' }
        '^(is|has|can|does|should)[A-Z]'          { return 'bool' }
        default                                   { return 'TODO' }
    }
}

# Parse the inside of a `Usage:` signature into Doc::Req / Doc::Opt lines.
# `[, x]` marks the start of the optional tail; `...` is a vararg.
function ConvertTo-Fields([string] $argText) {
    $fields = @()
    if (-not $argText.Trim()) { return $fields }

    $optionalFrom = $argText.IndexOf('[')
    $clean = $argText -replace '[\[\]]', ''
    $parts = $clean -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }

    $consumed = 0
    foreach ($p in $parts) {
        $bare = $p -replace '^"', '' -replace '"$', ''
        $consumed += $p.Length
        if ($bare -eq '...') {
            $fields += '    Game::Doc::Vararg("TODO"), // TODO: type'
            continue
        }
        $type = Get-TypeGuess $bare
        $todo = if ($type -eq 'TODO') { ' // TODO: type' } else { '' }
        $isOptional = $optionalFrom -ge 0 -and $argText.IndexOf($p) -gt $optionalFrom
        if ($isOptional) {
            $fields += "    Game::Doc::Opt(`"$bare`", `"$type`"),$todo"
        } else {
            $fields += "    Game::Doc::Req(`"$bare`", `"$type`"),$todo"
        }
    }
    return $fields
}

$summaries = Read-ApiSummaries
Write-Host "docs/API.md: $($summaries.Keys.Count) summaries indexed"

$files = @()
foreach ($p in $Path) { $files += Get-ChildItem -Path $p -File -Recurse -ErrorAction SilentlyContinue }
$files = $files | Sort-Object FullName -Unique

$written = 0
foreach ($file in $files) {
    $text = Get-Content $file.FullName -Raw
    $rel = $file.FullName.Substring($repo.Length + 1) -replace '\\', '/'

    # Registrations that already carry a descriptor end with `, &kSomething)`.
    $globals = [regex]::Matches($text, 'RegisterGlobalFunction\(\s*"([^"]+)"\s*,\s*&(\w+)\s*\)')
    $glue    = [regex]::Matches($text, 'RegisterGlueFunction\(\s*"([^"]+)"\s*,\s*&(\w+)\s*\)')
    $tables  = [regex]::Matches($text, 'RegisterTableFunction\(\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*&(\w+)\s*\)')
    if ($globals.Count + $glue.Count + $tables.Count -eq 0) { continue }

    # `Usage: Name(a [, b])` strings, keyed by the bare function name.
    $usage = @{}
    foreach ($m in [regex]::Matches($text, 'Usage:\s*([A-Za-z_][A-Za-z0-9_.:]*)\s*\(([^)"]*)\)')) {
        $short = ($m.Groups[1].Value -split '[.:]')[-1]
        if (-not $usage.ContainsKey($short)) { $usage[$short] = $m.Groups[2].Value }
    }

    $out = New-Object System.Collections.Generic.List[string]
    $out.Add("// Descriptor skeletons for $rel")
    $out.Add('//')
    $out.Add('// Paste into the file''s anonymous namespace, under a')
    $out.Add('//   // --- Documentation ---')
    $out.Add('// banner, then add the `&kName` argument to each Register* call.')
    $out.Add('//')
    $out.Add('// VERIFY EVERY LINE against the Script_* body. Types and optionality are')
    $out.Add('// guessed from the Usage: string; summaries come from docs/API.md, which')
    $out.Add('// is stale in places. The body is the authority.')
    $out.Add('')

    $emit = {
        param($luaName, $system)
        $short = ($luaName -split '\.')[-1]
        # One symbol per REGISTERED NAME, not per short name: a global and a
        # table function often share a short name and would collide.
        $sym = 'k' + (($luaName -replace '[^A-Za-z0-9]', ''))
        $summary = if ($summaries.ContainsKey($short)) { $summaries[$short] } else { 'TODO: one sentence.' }
        $argText = if ($usage.ContainsKey($short)) { $usage[$short] } else { $null }

        $out.Add("// $luaName")
        if ($null -eq $argText) {
            $out.Add("// TODO: no Usage: string found - read the Script_* body for the arguments.")
        }
        $fields = if ($argText) { ConvertTo-Fields $argText } else { @() }
        $argsRef = '{}'
        if ($fields.Count -gt 0) {
            $out.Add("const Game::Doc::Field ${sym}Args[] = {")
            $fields | ForEach-Object { $out.Add($_) }
            $out.Add('};')
            $argsRef = "${sym}Args"
        }
        $out.Add("const Game::Doc::Field ${sym}Rets[] = {")
        $out.Add('    // TODO: one entry per pushed value, in order.')
        $out.Add('    Game::Doc::Req("TODO", "TODO"),')
        $out.Add('};')
        $sysArg = if ($system) { ", `"$system`"" } else { '' }
        $out.Add("const Game::Doc::Function $sym{")
        $out.Add("    `"$summary`",")
        $out.Add("    $argsRef, ${sym}Rets$sysArg};")
        $out.Add('')
    }

    foreach ($m in $globals) { & $emit $m.Groups[1].Value 'TODOGlobals' }
    foreach ($m in $glue)    { & $emit $m.Groups[1].Value 'TODOGlobals' }
    foreach ($m in $tables)  { & $emit "$($m.Groups[1].Value).$($m.Groups[2].Value)" $null }

    $dest = Join-Path $OutDir ($rel -replace '^src/', '')
    $dest = [IO.Path]::ChangeExtension($dest, $null) + 'skel.cpp'
    $destDir = Split-Path -Parent $dest
    if (-not (Test-Path $destDir)) { New-Item -ItemType Directory -Force $destDir | Out-Null }
    $noBom = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($dest, ($out -join "`r`n") + "`r`n", $noBom)
    $written++
    Write-Host "  $rel -> $dest"
}

Write-Host ""
Write-Host "$written skeleton file(s) written to $OutDir."
Write-Host "Nothing under src/ was modified."
