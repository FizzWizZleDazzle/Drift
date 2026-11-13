# Drift toolchain installer for Windows.
#
#   irm https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.ps1 | iex
#
# With options:
#
#   & ([scriptblock]::Create((irm https://raw.githubusercontent.com/FizzWizZleDazzle/Drift/main/install.ps1))) -Version 0.3.0
#
# The Windows side of install.sh, with the same release contract: it resolves
# a release of FizzWizZleDazzle/Drift (latest, or -Version), downloads
# drift-windows-x86_64.zip, checks it against the release's sha256sums.txt,
# and unpacks it under %USERPROFILE%\.drift. bin\ goes on the user PATH. Every
# installed path is recorded in <prefix>\receipt, so a later run replaces an
# old version exactly and -Uninstall removes it. -From replaces the release
# with a folder or URL holding the zip and sha256sums.txt.

param(
    [string]$Prefix = $(if ($env:DRIFT_PREFIX) { $env:DRIFT_PREFIX } else { Join-Path $HOME '.drift' }),
    [string]$Version = '',
    [string]$From = '',
    [switch]$Force,
    [switch]$Uninstall,
    [switch]$NoModifyPath
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Repo = if ($env:DRIFT_GH_REPO) { $env:DRIFT_GH_REPO } else { 'FizzWizZleDazzle/Drift' }
$GhUrl = "https://github.com/$Repo"
$Prefix = [System.IO.Path]::GetFullPath($Prefix)
$BinDir = Join-Path $Prefix 'bin'
$Receipt = Join-Path $Prefix 'receipt'
$Target = 'windows-x86_64'
$Asset = "drift-$Target.zip"

# Errors are thrown, never `exit`: under `irm ... | iex` an exit would close
# the terminal the installer was run from.
function Fail([string]$Message) {
    throw "install.ps1: $Message"
}

function Get-ReceiptVersion {
    if (-not (Test-Path $Receipt)) { return '' }
    foreach ($line in Get-Content $Receipt) {
        if ($line -like 'version *') { return $line.Substring(8) }
    }
    return ''
}

function Get-ReceiptFiles {
    if (-not (Test-Path $Receipt)) { return @() }
    return @(Get-Content $Receipt | Where-Object { $_ -like 'file *' } | ForEach-Object { $_.Substring(5) })
}

function Remove-EmptyFolders([string]$Root) {
    if (-not (Test-Path $Root)) { return }
    Get-ChildItem $Root -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending |
        Where-Object { -not (Get-ChildItem $_.FullName -Force) } | Remove-Item -Force
    if (-not (Get-ChildItem $Root -Force)) { Remove-Item $Root -Force }
}

# A location that is not a URL is a local path, copied rather than downloaded.
function Get-File([string]$Source, [string]$Dest) {
    if ($Source -match '^https?://') {
        Invoke-WebRequest -UseBasicParsing -Uri $Source -OutFile $Dest
    } else {
        Copy-Item -LiteralPath $Source -Destination $Dest
    }
}

function Get-UserPath {
    return [Environment]::GetEnvironmentVariable('Path', 'User')
}

function Invoke-DriftInstall {
    if ($Uninstall) {
        if (-not (Test-Path $Receipt)) {
            Write-Host "Nothing to uninstall: no Drift install found at $Prefix"
            return
        }
        Write-Host "Removing drift $(Get-ReceiptVersion) from $Prefix"
        foreach ($rel in Get-ReceiptFiles) {
            $path = Join-Path $Prefix $rel
            if (Test-Path $path) { Remove-Item -Force $path }
        }
        Remove-Item -Force $Receipt
        Remove-EmptyFolders $Prefix
        $userPath = Get-UserPath
        if ($userPath) {
            $kept = ($userPath -split ';' | Where-Object { $_ -and ($_ -ne $BinDir) }) -join ';'
            if ($kept -ne $userPath) {
                [Environment]::SetEnvironmentVariable('Path', $kept, 'User')
                Write-Host "Took $BinDir off your PATH."
            }
        }
        Write-Host 'Removed.'
        return
    }

    if ($From -and $Version) { Fail '-From and -Version do not go together; -From names the files to install' }

    $Tmp = Join-Path ([System.IO.Path]::GetTempPath()) ("drift-install-" + [guid]::NewGuid())
    New-Item -ItemType Directory -Path $Tmp | Out-Null
    try {
        $Sums = Join-Path $Tmp 'sha256sums.txt'
        $Release = ''
        if ($From) {
            $Base = $From.TrimEnd('/', '\')
            $sep = if ($Base -match '^https?://') { '/' } else { '\' }
            try { Get-File "$Base$($sep)sha256sums.txt" $Sums }
            catch { Fail "$Base has no sha256sums.txt; -From names a folder or URL holding $Asset and sha256sums.txt" }
        } else {
            $sep = '/'
            if ($Version) {
                # Tags are vX.Y.Z, but -Version takes the bare number too.
                $Tag = if ($Version.StartsWith('v')) { $Version } else { "v$Version" }
            } else {
                # /releases/latest redirects to /releases/tag/<tag>.
                $response = Invoke-WebRequest -UseBasicParsing -Uri "$GhUrl/releases/latest" -MaximumRedirection 5
                $final = $response.BaseResponse.ResponseUri
                if (-not $final) { $final = $response.BaseResponse.RequestMessage.RequestUri }
                $Tag = ($final.AbsolutePath -split '/')[-1]
                if (-not $Tag -or $Tag -eq 'latest' -or $Tag -eq 'releases') {
                    Fail "could not resolve the latest release of $Repo. See $GhUrl/releases"
                }
            }
            $Base = "$GhUrl/releases/download/$Tag"
            try { Get-File "$Base/sha256sums.txt" $Sums }
            catch { Fail "no release $Tag of $Repo with a sha256sums.txt. See $GhUrl/releases" }
            $Release = $Tag.TrimStart('v')
        }

        $Want = ''
        foreach ($line in Get-Content $Sums) {
            $fields = $line -split '\s+', 2
            if ($fields.Count -eq 2 -and $fields[1].TrimStart('*') -eq $Asset) { $Want = $fields[0].ToLower() }
        }
        if (-not $Want) { Fail "$Base publishes no $Asset, so there is no Drift for $Target there" }

        $Installed = Get-ReceiptVersion
        if (-not $Force -and $Release -and $Installed -eq $Release) {
            Write-Host "Already up to date: drift $Installed at $Prefix. Use -Force to reinstall."
            return
        }

        Write-Host "Downloading $Asset"
        $Zip = Join-Path $Tmp $Asset
        try { Get-File "$Base$sep$Asset" $Zip } catch { Fail "download failed: $Base$sep$Asset" }
        $Got = (Get-FileHash -Algorithm SHA256 $Zip).Hash.ToLower()
        if ($Got -ne $Want) {
            Fail "checksum mismatch for $Asset`n  expected $Want`n  got      $Got`nNothing was installed."
        }

        $Unpacked = Join-Path $Tmp 'x'
        Expand-Archive -Path $Zip -DestinationPath $Unpacked
        $Root = Join-Path $Unpacked "drift-$Target"
        if (-not (Test-Path (Join-Path $Root 'bin\drift.exe'))) { $Root = $Unpacked }
        if (-not (Test-Path (Join-Path $Root 'bin\drift.exe'))) { Fail "$Asset has no bin\drift.exe in it" }
        if (-not (Test-Path (Join-Path $Root 'lib\drift\web'))) { Fail "$Asset has no lib\drift\web in it" }

        if (-not $Release) {
            # Installing from -From: the program says which version it is.
            $out = & (Join-Path $Root 'bin\drift.exe') --version 2>$null
            if ("$out" -match '^drift\s+(\S+)') { $Release = $Matches[1] }
            if (-not $Release) { Fail "the drift program in $Asset does not run on this machine" }
            if (-not $Force -and $Installed -eq $Release) {
                Write-Host "Already up to date: drift $Installed at $Prefix. Use -Force to reinstall."
                return
            }
        }

        if ($Installed) { Write-Host "Installing drift $Release over $Installed" }
        else { Write-Host "Installing drift $Release" }

        $rootFull = (Resolve-Path $Root).Path.TrimEnd('\')
        $files = @(Get-ChildItem $Root -Recurse -File | ForEach-Object {
            $_.FullName.Substring($rootFull.Length + 1).Replace('\', '/')
        } | Sort-Object)
        foreach ($rel in $files) {
            $dest = Join-Path $Prefix $rel
            New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
            Copy-Item -Force (Join-Path $Root $rel) $dest
        }
        foreach ($stale in Get-ReceiptFiles) {
            if ($files -notcontains $stale) {
                $path = Join-Path $Prefix $stale
                if (Test-Path $path) { Remove-Item -Force $path }
            }
        }
        $lines = @("version $Release", "target $Target") + ($files | ForEach-Object { "file $_" })
        Set-Content -Path $Receipt -Value $lines -Encoding ascii
        Remove-EmptyFolders $Prefix

        $userPath = Get-UserPath
        $onPath = $userPath -and (($userPath -split ';') -contains $BinDir)
        if (-not $onPath) {
            if ($NoModifyPath) {
                Write-Host "Add $BinDir to your PATH to run drift."
            } else {
                $newPath = if ($userPath) { "$BinDir;$userPath" } else { $BinDir }
                [Environment]::SetEnvironmentVariable('Path', $newPath, 'User')
                Write-Host "Added $BinDir to your PATH. Open a new terminal to pick it up."
            }
        }

        Write-Host ''
        Write-Host "Installed drift $Release under $Prefix."
        Write-Host ''
        Write-Host 'Get started:'
        Write-Host '  drift run path\to\game          play a game folder'
        Write-Host '  drift build --web path\to\game  write it as a web page'
    } finally {
        Remove-Item -Recurse -Force $Tmp -ErrorAction SilentlyContinue
    }
}

try {
    Invoke-DriftInstall
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
}
