# Cas du fil CLI : amk build produit un zip que tar.exe lit, amk install le pose dans le kit (SHA-256 verifie), puis
# export, import et remove passent avec tar.exe. Pas d'Apple Music. Demande dist\cli\amk.exe.

Register-Case -Id 'cli-build-installe' -Title 'amk build, install, export, import et remove avec tar.exe' -Action {
    param($Ctx)

    $amk = Join-Path $script:KitRoot 'cli\amk.exe'
    if (-not (Test-Path -LiteralPath $amk)) {
        Add-Skip -Ctx $Ctx -Name 'amk.exe present dans dist\cli' -Reason 'absent : ajouter la copie de amk.exe au job de build'
        return
    }

    $work = Join-Path ([IO.Path]::GetTempPath()) ('amk-banc-work-' + [guid]::NewGuid().ToString('N'))
    $kit = Join-Path $work 'kit'
    New-Item -ItemType Directory -Path $work | Out-Null
    try {
        Push-Location -LiteralPath $work
        & $amk new extension bench-cli | Out-Null
        $folder = Join-Path $work 'bench-cli'
        $zip = Join-Path $work 'bench-cli-0.1.0.zip'

        $built = (& $amk --kit-dir $kit build $folder $zip 2>&1) | Out-String
        Add-Check -Ctx $Ctx -Name 'amk build ecrit le zip' -Passed ((Test-Path -LiteralPath $zip) -and ($LASTEXITCODE -eq 0)) -Detail $built.Trim()
        if (-not (Test-Path -LiteralPath $zip)) { return }

        # The tar of Windows, by full path: the job runs under Git Bash, whose GNU tar comes first in PATH.
        $windowsTar = Join-Path $env:SystemRoot 'System32\tar.exe'
        $listing = (& $windowsTar -tf $zip 2>&1) | Out-String
        Add-Check -Ctx $Ctx -Name 'tar.exe lit le zip compile' -Passed ($listing -match 'manifest\.json') -Detail $listing.Trim()

        # The catalogue entry printed by build, with the address of the local archive.
        $entry = $built.Substring($built.IndexOf('{')) | ConvertFrom-Json
        $entry.url = 'file:///' + ($zip -replace '\\', '/')
        $catalog = Join-Path $work 'catalog.json'
        $document = @{ catalogVersion = 1; extensions = @($entry) } | ConvertTo-Json -Depth 6
        [IO.File]::WriteAllText($catalog, $document, (New-Object Text.UTF8Encoding($false)))

        $installed = (& $amk --kit-dir $kit install bench-cli --catalog $catalog --yes 2>&1) | Out-String
        $installCode = $LASTEXITCODE
        $manifest = Join-Path $kit 'extensions\bench-cli\manifest.json'
        Add-Check -Ctx $Ctx -Name 'amk install pose l element dans le kit' -Passed (($installCode -eq 0) -and (Test-Path -LiteralPath $manifest)) -Detail $installed.Trim()

        $archive = Join-Path $work 'export.zip'
        $exported = (& $amk --kit-dir $kit export $archive 2>&1) | Out-String
        Add-Check -Ctx $Ctx -Name 'amk export ecrit l archive' -Passed (($LASTEXITCODE -eq 0) -and (Test-Path -LiteralPath $archive)) -Detail $exported.Trim()

        Remove-Item -LiteralPath (Join-Path $kit 'extensions') -Recurse -Force
        $imported = (& $amk --kit-dir $kit import $archive --yes 2>&1) | Out-String
        $importCode = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name 'amk import restaure l element avec tar.exe' -Passed (($importCode -eq 0) -and (Test-Path -LiteralPath $manifest)) -Detail $imported.Trim()

        $removed = (& $amk --kit-dir $kit remove bench-cli --yes 2>&1) | Out-String
        $removeCode = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name 'amk remove retire l element (sauvegarde faite avant)' -Passed (($removeCode -eq 0) -and -not (Test-Path -LiteralPath $manifest)) -Detail $removed.Trim()
    }
    finally {
        Pop-Location -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
    }
}
