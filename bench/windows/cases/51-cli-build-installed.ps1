# CLI case: ampify build makes a zip that tar.exe reads, ampify install puts it in the Ampify folder (SHA-256 checked),
# then export, import and remove work with tar.exe. No Apple Music. Needs dist\cli\ampify.exe.

Register-Case -Id 'cli-build-installed' -Title 'ampify build, install, export, import and remove with tar.exe' -Requires 'kit:cli' -Action {
    param($Ctx)

    $ampify = Join-Path $script:BenchKitRoot 'cli\ampify.exe'

    $work = Join-Path ([IO.Path]::GetTempPath()) ('ampify-bench-work-' + [guid]::NewGuid().ToString('N'))
    $kit = Join-Path $work 'kit'
    New-Item -ItemType Directory -Path $work | Out-Null
    try {
        Push-Location -LiteralPath $work
        & $ampify new extension bench-cli | Out-Null
        $folder = Join-Path $work 'bench-cli'
        $zip = Join-Path $work 'bench-cli-0.1.0.zip'
        # build refuses the starter's own author and description: fill them in, as an author would.
        $manifest = Join-Path $folder 'manifest.json'
        $text = [IO.File]::ReadAllText($manifest).Replace('your-github-username', 'bench').Replace('Describe it here.', 'A bench extension.')
        [IO.File]::WriteAllText($manifest, $text)

        $built = (& $ampify --kit-dir $kit build $folder $zip 2>&1) | Out-String
        Add-Check -Ctx $Ctx -Name 'ampify build writes the zip' -Passed ((Test-Path -LiteralPath $zip) -and ($LASTEXITCODE -eq 0)) -Detail $built.Trim()
        if (-not (Test-Path -LiteralPath $zip)) { return }

        # The tar of Windows, by full path: the job runs under Git Bash, whose GNU tar comes first in PATH.
        $windowsTar = Join-Path $env:SystemRoot 'System32\tar.exe'
        $listing = (& $windowsTar -tf $zip 2>&1) | Out-String
        Add-Check -Ctx $Ctx -Name 'tar.exe reads the built zip' -Passed ($listing -match 'manifest\.json') -Detail $listing.Trim()

        # The catalog entry printed by build, with the address of the local archive.
        $entry = $built.Substring($built.IndexOf('{')) | ConvertFrom-Json
        $entry.url = 'file:///' + ($zip -replace '\\', '/')
        $catalog = Join-Path $work 'catalog.json'
        $document = @{ catalogVersion = 1; extensions = @($entry) } | ConvertTo-Json -Depth 6
        [IO.File]::WriteAllText($catalog, $document, (New-Object Text.UTF8Encoding($false)))

        $installed = (& $ampify --kit-dir $kit install bench-cli --catalog $catalog --yes 2>&1) | Out-String
        $installCode = $LASTEXITCODE
        $manifest = Join-Path $kit 'extensions\bench-cli\manifest.json'
        Add-Check -Ctx $Ctx -Name 'ampify install puts the element in the Ampify folder' -Passed (($installCode -eq 0) -and (Test-Path -LiteralPath $manifest)) -Detail $installed.Trim()

        $archive = Join-Path $work 'export.zip'
        $exported = (& $ampify --kit-dir $kit export $archive 2>&1) | Out-String
        Add-Check -Ctx $Ctx -Name 'ampify export writes the archive' -Passed (($LASTEXITCODE -eq 0) -and (Test-Path -LiteralPath $archive)) -Detail $exported.Trim()

        Remove-Item -LiteralPath (Join-Path $kit 'extensions') -Recurse -Force
        $imported = (& $ampify --kit-dir $kit import $archive --yes 2>&1) | Out-String
        $importCode = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name 'ampify import restores the element with tar.exe' -Passed (($importCode -eq 0) -and (Test-Path -LiteralPath $manifest)) -Detail $imported.Trim()

        $removed = (& $ampify --kit-dir $kit remove bench-cli --yes 2>&1) | Out-String
        $removeCode = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name 'ampify remove removes the element (backup made first)' -Passed (($removeCode -eq 0) -and -not (Test-Path -LiteralPath $manifest)) -Detail $removed.Trim()
    }
    finally {
        Pop-Location -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
