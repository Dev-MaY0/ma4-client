<#
.SYNOPSIS
    Commits everything that changed in this project and pushes it to the ma4-client GitHub repository.

.DESCRIPTION
    * Refuses to run unless the git top-level folder is this project (it must never commit your home folder).
    * Sets up the repository on first use (git init, origin = ma4-client, history of origin/main kept).
    * With -Version: updates the version string in src/client/Ma4.h (and the UI harness), commits and tags v<Version>.
    * Rebases onto origin/main if the remote has new commits, then pushes. It never force-pushes.

.EXAMPLE
    .\tools\publish.ps1 -Version 1.0.2 -Message "Fix the replay map colours"

.EXAMPLE
    .\tools\publish.ps1 -Message "Tweak notification timing"    # plain commit + push, no version change

.EXAMPLE
    .\tools\publish.ps1 -DryRun                                  # show what would happen
#>
param(
    [string]$Version = "",
    [string]$Message = "",
    [switch]$NoPush,
    [switch]$NoTag,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"

$RemoteUrl = "https://github.com/Dev-MaY0/ma4-client.git"
$Branch = "main"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Invoke-Git {
    # simple function (no param block) so that tokens such as -F / -b reach git untouched
    # git writes progress ("From https://...") to stderr; Windows PowerShell 5.1 would turn that into a terminating error
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $output = & git -C $Root @args 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previous
    }
    foreach ($line in $output) { Write-Host "  $line" }
    return $code
}

function Get-GitText {
    $text = & git -C $Root @args 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    return ($text -join "`n").Trim()
}

# ------------------------------------------------------------------------------------------------ repository setup
if (-not (Test-Path (Join-Path $Root ".git"))) {
    Write-Host "No repository in $Root - initialising it for $RemoteUrl"
    if ($DryRun) { Write-Host "(dry run) would run: git init, remote add origin, fetch, reset"; exit 0 }
    if ((Invoke-Git init -b $Branch) -ne 0) { throw "git init failed" }
    Invoke-Git remote add origin $RemoteUrl | Out-Null
    if ((Invoke-Git fetch origin $Branch) -ne 0) { throw "git fetch failed (is the repository reachable / are you signed in?)" }
    # adopt the history of the remote branch but keep every local file as it is
    if ((Invoke-Git reset --mixed "origin/$Branch") -ne 0) { throw "git reset failed" }
}

$top = Get-GitText rev-parse --show-toplevel
if (-not $top -or ([IO.Path]::GetFullPath($top).TrimEnd('\', '/') -ne [IO.Path]::GetFullPath($Root).TrimEnd('\', '/'))) {
    throw "Refusing to continue: the git top-level folder is '$top' but this project is '$Root'."
}

$origin = Get-GitText remote get-url origin
if (-not $origin) {
    Invoke-Git remote add origin $RemoteUrl | Out-Null
} elseif ($origin -ne $RemoteUrl) {
    Write-Warning "origin points to '$origin' (expected '$RemoteUrl'). Leaving it unchanged."
}

# ------------------------------------------------------------------------------------------------ version bump
if ($Version) {
    if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Version must look like 1.2.3 (got '$Version')." }

    $utf8 = New-Object System.Text.UTF8Encoding($false)
    foreach ($rel in @("src\client\Ma4.h", "tools\ui-harness\harness.cpp")) {
        $file = Join-Path $Root $rel
        if (-not (Test-Path $file)) { continue }
        $text = [IO.File]::ReadAllText($file)
        $new = [regex]::Replace($text, 'version = "v[^"]*"', "version = `"v$Version`"")
        $new = [regex]::Replace($new, 'L"v\d+\.\d+\.\d+"', "L`"v$Version`"")
        if ($new -ne $text) {
            if ($DryRun) { Write-Host "(dry run) would update version in $rel" } else { [IO.File]::WriteAllText($file, $new, $utf8) }
        }
    }

    $changelog = Join-Path $Root "CHANGELOG.md"
    if ((Test-Path $changelog) -and -not (Select-String -Path $changelog -SimpleMatch "## [$Version]" -Quiet)) {
        Write-Warning "CHANGELOG.md has no '## [$Version]' section - add one before publishing."
    }
}

# ------------------------------------------------------------------------------------------------ commit
if ($DryRun) {
    Write-Host "(dry run) files that would be committed:"
    Invoke-Git status --short | Out-Null
    exit 0
}

if ((Invoke-Git add -A) -ne 0) { throw "git add failed" }

if (-not $Message) {
    if ($Version) { $Message = "Release v$Version" } else { $Message = "Update " + (Get-Date -Format "yyyy-MM-dd HH:mm") }
}

& git -C $Root diff --cached --quiet
$hasChanges = ($LASTEXITCODE -ne 0)
if ($hasChanges) {
    $msgFile = [IO.Path]::GetTempFileName()
    try {
        [IO.File]::WriteAllText($msgFile, $Message, (New-Object System.Text.UTF8Encoding($false)))
        if ((Invoke-Git commit -F $msgFile) -ne 0) { throw "git commit failed" }
    } finally {
        Remove-Item $msgFile -ErrorAction SilentlyContinue
    }
} else {
    Write-Host "Nothing new to commit."
}

# ------------------------------------------------------------------------------------------------ tag
$tag = $null
if ($Version -and -not $NoTag) {
    $tag = "v$Version"
    & git -C $Root rev-parse -q --verify "refs/tags/$tag" *> $null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Tag $tag already exists - not moving it."
    } elseif ((Invoke-Git tag -a $tag -m $Message) -ne 0) {
        throw "git tag failed"
    }
}

if ($NoPush) { Write-Host "Done (push skipped)."; exit 0 }

# ------------------------------------------------------------------------------------------------ sync + push (never forced)
if ((Invoke-Git fetch origin $Branch) -ne 0) { throw "git fetch failed" }
& git -C $Root merge-base --is-ancestor "origin/$Branch" HEAD
if ($LASTEXITCODE -ne 0) {
    Write-Host "origin/$Branch has commits that are not here - rebasing on top of them."
    if ((Invoke-Git pull --rebase --autostash origin $Branch) -ne 0) {
        Invoke-Git rebase --abort | Out-Null
        throw "Rebase onto origin/$Branch failed. Resolve it manually, then run this script again."
    }
}

if ((Invoke-Git push origin "HEAD:$Branch") -ne 0) {
    throw "git push failed. Sign in to GitHub (Git Credential Manager) or provide a personal access token, then run this script again."
}
if ($tag) {
    if ((Invoke-Git push origin $tag) -ne 0) { Write-Warning "Pushing tag $tag failed (it may already exist on the remote)." }
}
Write-Host "Published to $RemoteUrl ($Branch)."
