# M5 : le kit de test, lancé en mode /auto comme un double-clic, écrit son rapport et son archive. Si Apple Music est
# installé sur le runner, le kit doit aussi le lancer par le raccourci. Les traces ETW demandent les droits
# administrateur : sans eux, le test passe avec /sansetl.

Register-Case -Id 'kit-de-test' -Title 'Kit de test M5 : rapport, archive et lancement par le raccourci' -Action {
    param($Ctx)

    $testKit = Join-Path $script:KitRoot 'diffusion\AMK-Test.exe'
    if (-not (Test-Path -LiteralPath $testKit)) {
        Add-Check -Ctx $Ctx -Name 'AMK-Test.exe présent dans diffusion' -Passed $false -Detail $testKit
        return
    }

    # `net session` ne réussit que dans un processus élevé : sans lui, le kit ne doit pas demander l'accord UAC.
    $null = & net.exe session 2>$null
    $elevated = ($LASTEXITCODE -eq 0)

    Reset-KitState
    $arguments = @('/auto', '/attente:20')
    if (-not $elevated) { $arguments += '/sansetl' }
    # /attente:20 plus the install, the launch (90 s at most) and the collection: 6 minutes is ample.
    $code = Invoke-KitProgram -Path $testKit -Arguments $arguments -TimeoutSeconds 360
    Add-Check -Ctx $Ctx -Name 'kit de test : pas d''erreur (code 0, 2 ou 4)' -Passed ($code -in 0, 2, 4) -Detail "code $code"

    $pointer = Join-Path $script:KitDir 'rapports\dernier.txt'
    $run = if (Test-Path -LiteralPath $pointer) { (Get-Content -LiteralPath $pointer -Raw -Encoding UTF8).Trim() } else { '' }
    if ($code -notin 0, 2, 4) {
        # Show how far the kit got: its own step journal, from the newest report folder.
        $latest = Get-ChildItem -LiteralPath (Join-Path $script:KitDir 'rapports') -Directory -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($latest) {
            Write-Host "----- deroulement.txt ($($latest.Name))"
            Get-Content -LiteralPath (Join-Path $latest.FullName 'deroulement.txt') -ErrorAction SilentlyContinue |
                ForEach-Object { Write-Host "  $_" }
        }
    }
    Add-Check -Ctx $Ctx -Name 'dernier rapport désigné (rapports\dernier.txt)' -Passed ([bool]$run -and (Test-Path -LiteralPath $run))
    if (-not $run) { return }

    $report = Join-Path $run 'rapport.txt'
    Add-Check -Ctx $Ctx -Name 'rapport.txt écrit' -Passed (Test-Path -LiteralPath $report)
    Add-Check -Ctx $Ctx -Name 'archive .zip créée à côté du dossier' -Passed (Test-Path -LiteralPath ($run + '.zip'))
    $text = if (Test-Path -LiteralPath $report) { Get-Content -LiteralPath $report -Raw -Encoding UTF8 } else { '' }
    Add-Check -Ctx $Ctx -Name 'rapport : kit installé' -Passed ($text -match '\[OK\] *Kit installé')

    if ($script:AppleMusicInstalled) {
        Add-Check -Ctx $Ctx -Name 'Apple Music lancé par le kit (raccourci du menu Démarrer)' `
            -Passed ($text -match 'Apple Music lancé par le kit : oui') -Detail "version $($script:AppleMusicVersion)"
        $known = $script:KnownVersions -contains $script:AppleMusicVersion
        if ($known) {
            Add-Check -Ctx $Ctx -Name 'version testée : le noyau est autorisé' -Passed ($text -match 'Version testée, noyau autorisé : oui')
        } else {
            Add-Check -Ctx $Ctx -Name 'version non testée : le noyau reste inactif, sans bloquer Apple Music' `
                -Passed ($text -match 'non, le noyau reste inactif') -Detail "version $($script:AppleMusicVersion)"
        }
    } else {
        Add-Skip -Ctx $Ctx -Name 'lancement par le raccourci' -Reason 'Apple Music absent du runner'
    }

    if ($elevated) {
        $trace = Join-Path $run 'trace.etl'
        # The trace is for the owner's PC. On the runner logman may refuse it: that is reported, not failed.
        $traced = (Test-Path -LiteralPath $trace) -and (Get-Item -LiteralPath $trace).Length -gt 0
        $traceLines = @(Get-Content -LiteralPath (Join-Path $run 'deroulement.txt') -ErrorAction SilentlyContinue |
            Where-Object { $_ -match 'Trace|logman' }) -join ' | '
        Add-Info -Ctx $Ctx -Name 'trace ETW' -Detail $(if ($traced) { 'enregistrée' } else { "non enregistrée : $traceLines" })
    } else {
        Add-Skip -Ctx $Ctx -Name 'trace ETW' -Reason 'runner non élevé : /sansetl'
    }
    Add-Check -Ctx $Ctx -Name 'événements Windows lus (evenements-application.txt)' `
        -Passed (Test-Path -LiteralPath (Join-Path $run 'evenements-application.txt'))

    Copy-Item -LiteralPath $run -Destination (Join-Path $Ctx.Dir 'rapport') -Recurse -Force -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath ($run + '.zip') -Destination $Ctx.Dir -Force -ErrorAction SilentlyContinue
    Close-AppleMusic | Out-Null
}
