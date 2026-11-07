# CLI case: tar.exe and curl.exe are there, and https is the only protocol accepted for a remote catalog.
# Run by the bench (docs/development/testing.md). Does not need Apple Music. Needs dist\cli\ampify.exe.

Register-Case -Id 'cli-windows-tools' -Title 'ampify finds tar.exe and curl.exe, and refuses http://' -Requires 'kit:cli' -Action {
    param($Ctx)

    $ampify = Join-Path $script:BenchKitRoot 'cli\ampify.exe'

    $kit = Join-Path ([IO.Path]::GetTempPath()) ('ampify-bench-kit-' + [guid]::NewGuid().ToString('N'))
    try {
        $doctor = (& $ampify --kit-dir $kit doctor 2>&1) | Out-String
        $doctorCode = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name 'ampify doctor ends without error' -Passed ($doctorCode -eq 0) -Detail $doctor.Trim()
        Add-Check -Ctx $Ctx -Name 'tar.exe found by ampify doctor' -Passed ($doctor -match 'tar\.exe')
        Add-Check -Ctx $Ctx -Name 'curl.exe found by ampify doctor' -Passed ($doctor -match 'curl\.exe')

        $httpCatalog = 'http://example.com/catalog/catalog.json'
        $refused = (& $ampify --kit-dir $kit market --catalog $httpCatalog 2>&1) | Out-String
        $refusedCode = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name 'http:// catalog refused' -Passed ($refusedCode -ne 0) -Detail $refused.Trim()

        # A raw file of a private repository would need a token. The check is about curl.exe fetching over https:
        # a public JSON file that is not a catalog must get past the download and fail only as an invalid catalog.
        $httpsCatalog = 'https://raw.githubusercontent.com/spicetify/marketplace/main/package.json'
        $listed = (& $ampify --kit-dir $kit market --catalog $httpsCatalog 2>&1) | Out-String
        $downloaded = -not ($listed -match 'download failed|curl: \(')
        Add-Check -Ctx $Ctx -Name 'https:// downloads with curl.exe (public file)' -Passed $downloaded -Detail $listed.Trim()
    }
    finally {
        Remove-Item -LiteralPath $kit -Recurse -Force -ErrorAction SilentlyContinue
    }
}
