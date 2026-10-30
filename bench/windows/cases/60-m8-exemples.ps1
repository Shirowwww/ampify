# Étape 1 du test M8, sans Apple Music : les quatre thèmes d'exemple se chargent sans problème avec amk_theme_check.
# Job : MSVC (xaml-m8), pas le banc MinGW. Attend $script:BenchKitRoot\m8\amk_theme_check.exe et une copie de
# themes/examples dans $script:BenchKitRoot\m8\themes\examples.

Register-Case -Id 'm8-exemples' -Title 'M8 : les thèmes d''exemple sont utilisables (amk_theme_check)' -Action {
    param($Ctx)

    $check = Join-Path $script:BenchKitRoot 'm8\amk_theme_check.exe'
    if (-not (Test-Path -LiteralPath $check)) {
        Add-Skip -Ctx $Ctx -Name 'amk_theme_check.exe présent' -Reason "pas de build M8 dans ce banc (MinGW ne compile pas XAML) : $check"
        return
    }

    $examples = Join-Path $script:BenchKitRoot 'm8\themes\examples'
    foreach ($name in 'minuit', 'texte-agrandi', 'bouton-arrondi', 'masquer-chrome') {
        $folder = Join-Path $examples $name
        # Le texte est écrit en UTF-8 par le C++ : on teste des mots sans accent, et le code de sortie.
        $output = (& $check $folder 2>&1 | Out-String)
        $code = $LASTEXITCODE
        Add-Check -Ctx $Ctx -Name "$name : code de sortie 0" -Passed ($code -eq 0) -Detail "code $code"
        Add-Check -Ctx $Ctx -Name "$name : thème utilisable" -Passed ($output -match 'utilisable')
        Add-Check -Ctx $Ctx -Name "$name : aucun problème signalé" -Passed ($output -notmatch 'Probl')
    }
}
