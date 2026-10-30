# Cas du fil CLI : tar.exe et curl.exe sont la, et le protocole https est le seul accepte pour un catalogue distant.
# Lance par le banc (docs/tests/banc-windows.md). Ne demande pas Apple Music. Demande dist\cli\amk.exe.

Register-Case -Id 'cli-outils-windows' -Title 'amk trouve tar.exe et curl.exe, et refuse http://' -Action {
    param($Ctx)

    $amk = Join-Path $script:KitRoot 'cli\amk.exe'
    if (-not (Test-Path -LiteralPath $amk)) {
        Add-Skip -Ctx $Ctx -Name 'amk.exe present dans dist\cli' -Reason 'absent : ajouter la copie de amk.exe au job de build'
        return
    }

    $kit = Join-Path ([IO.Path]::GetTempPath()) ('amk-banc-kit-' + [guid]::NewGuid().ToString('N'))
    try {
        $doctor = (& $amk --kit-dir $kit doctor 2>&1) | Out-String
        $doctorCode = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name 'amk doctor se termine sans erreur' -Passed ($doctorCode -eq 0) -Detail $doctor.Trim()
        Add-Check -Ctx $Ctx -Name 'tar.exe trouve par amk doctor' -Passed ($doctor -match 'tar\.exe')
        Add-Check -Ctx $Ctx -Name 'curl.exe trouve par amk doctor' -Passed ($doctor -match 'curl\.exe')

        $httpCatalog = 'http://example.com/catalog/catalog.json'
        $refused = (& $amk --kit-dir $kit market --catalog $httpCatalog 2>&1) | Out-String
        $refusedCode = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name 'catalogue http:// refuse' -Passed ($refusedCode -ne 0) -Detail $refused.Trim()

        # The catalog of the commit under test: main may not have one yet.
        $ref = if ($env:GITHUB_SHA) { $env:GITHUB_SHA } else { 'main' }
        $httpsCatalog = "https://raw.githubusercontent.com/Shirowwww/apple-music-mod-kit/$ref/catalog/catalog.json"
        $listed = (& $amk --kit-dir $kit market --catalog $httpsCatalog 2>&1) | Out-String
        $listedCode = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name 'catalogue https:// telecharge avec curl.exe' -Passed ($listedCode -eq 0) -Detail $listed.Trim()
    }
    finally {
        Remove-Item -LiteralPath $kit -Recurse -Force -ErrorAction SilentlyContinue
    }
}
