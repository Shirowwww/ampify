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

        # This repository is private, so its raw files need a token. The check is about curl.exe fetching over https:
        # a public JSON file that is not a catalog must get past the download and fail only as an invalid catalog.
        $httpsCatalog = 'https://raw.githubusercontent.com/spicetify/marketplace/main/package.json'
        $listed = (& $amk --kit-dir $kit market --catalog $httpsCatalog 2>&1) | Out-String
        $downloaded = -not ($listed -match 'téléchargement impossible|curl: \(')
        Add-Check -Ctx $Ctx -Name 'https:// telecharge avec curl.exe (fichier public)' -Passed $downloaded -Detail $listed.Trim()
    }
    finally {
        Remove-Item -LiteralPath $kit -Recurse -Force -ErrorAction SilentlyContinue
    }
}
