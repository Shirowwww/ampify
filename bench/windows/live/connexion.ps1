<#
.SYNOPSIS
Connecte Apple Music au compte de test sur le runner, par UI Automation, sans le kit.

.DESCRIPTION
1. Installe Apple Music (winget) s'il manque, puis restaure le cache de session chiffré s'il existe.
2. Lance Apple Music normalement (sans lanceur ni noyau : le kit n'est jamais présent pendant la connexion).
3. Déjà connecté : rien à faire. Sinon, avec -AllowSignIn : bouton de connexion, identifiant Apple et mot de passe
   (variables AMK_APPLE_ID et AMK_APPLE_PASSWORD), puis code de double authentification (AMK_CODE_2FA) si Apple le
   demande. Sans code alors qu'Apple en demande un : arrêt immédiat avec un message clair (code de sortie 2).
4. Après une nouvelle connexion : ferme Apple Music et écrit le cache de session chiffré.
Les identifiants ne sont jamais affichés ni écrits. Aucune capture d'écran entre la saisie de l'identifiant et la
connexion confirmée (l'identifiant serait visible).

.OUTPUTS
Code 0 : connecté. 1 : échec. 2 : code de double authentification nécessaire et absent. 3 : pas connecté, et
la connexion par mot de passe n'est pas permise (-AllowSignIn absent, par exemple sur un push).
#>
[CmdletBinding()]
param(
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\..\..\results-live'),
    [string]$CacheDir = (Join-Path ([IO.Path]::GetTempPath()) 'amk-session'),
    [switch]$AllowSignIn
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\harness.ps1')
. (Join-Path $PSScriptRoot '..\lib\uia.ps1')
. (Join-Path $PSScriptRoot 'live-common.ps1')

$dir = Join-Path $OutputRoot 'connexion'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$ctx = [pscustomobject]@{ Id = 'connexion'; Dir = $dir }

function Set-StepOutput([string]$Name, [string]$Value) {
    if ($env:GITHUB_OUTPUT) { "$Name=$Value" | Add-Content -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8 }
}

function Stop-Connexion([int]$Code, [string]$Message) {
    if ($Code -eq 0) { Write-Host "==> $Message" } else { Write-Host "::error::$(Protect-UiaText $Message)" }
    Set-StepOutput 'signed_in' $(if ($Code -eq 0) { 'true' } else { 'false' })
    exit $Code
}

Write-Host '==> Connexion à Apple Music (sans le kit)'
$state = Install-AppleMusic -LogFile (Join-Path $dir 'winget.log')
Write-Host "  Apple Music : $state"
if (-not (Get-AppleMusicFullName)) { Stop-Connexion 1 "Apple Music n'est pas installé sur ce runner ($state)" }

Stop-BenchProcesses
& (Join-Path $PSScriptRoot 'session-cache.ps1') -Mode Restore -CacheDir $CacheDir

Start-AppNormally
if (-not (Wait-AppleMusicReady -TimeoutSeconds 120)) {
    Save-Screenshot -Ctx $ctx -Name '01-demarrage' | Out-Null
    Write-VisibleSummary 'démarrage'
    Stop-Connexion 1 "Apple Music n'affiche pas sa fenêtre principale après 120 s"
}
Show-AppleMusicWindow | Out-Null
Start-Sleep -Seconds 5

$signed = Test-AppleMusicSignedIn
Write-Host "  déjà connecté : $signed"
if ($signed -eq 'oui') {
    Save-Screenshot -Ctx $ctx -Name '02-connecte' | Out-Null
    Set-StepOutput 'fresh' 'false'
    Close-AppleMusic | Out-Null
    Stop-Connexion 0 'Apple Music est déjà connecté (session restaurée)'
}
if (-not $AllowSignIn) {
    Close-AppleMusic | Out-Null
    Stop-Connexion 3 "Apple Music n'est pas connecté et la connexion par mot de passe n'est permise qu'au lancement manuel (workflow_dispatch)"
}
if (-not $env:AMK_APPLE_ID -or -not $env:AMK_APPLE_PASSWORD) {
    Close-AppleMusic | Out-Null
    Stop-Connexion 1 'secrets AMK_APPLE_ID et AMK_APPLE_PASSWORD absents : voir docs/tests/banc-windows.md'
}

# Before typing anything: the empty sign-in screen can be captured.
Save-Screenshot -Ctx $ctx -Name '01-avant-connexion' | Out-Null

# Gets to the account field: sign-in button, or the welcome screens in front of it.
$accountField = $null
for ($round = 0; $round -lt 8 -and -not $accountField; $round++) {
    $accountField = Find-Element -ControlType 'Edit' -VisibleOnly -Where {
        param($e) -not $e.Current.IsPassword -and ($e.Current.Name -match 'Apple|mail|Email|Phone|téléphone|Identifiant|Adresse|Account|Compte' -or
            $e.Current.AutomationId -match 'account|email|apple|user')
    }
    if ($accountField) { break }
    $button = Find-Element -NamePattern $script:Patterns.SignIn -VisibleOnly
    if (-not $button) { $button = Find-Element -NamePattern $script:Patterns.Onboarding -ControlType 'Button' -VisibleOnly }
    if ($button) {
        Write-Host "  clic : $(Format-Element $button) ($(Invoke-Click -Element $button))"
    }
    Start-Sleep -Seconds 4
}
if (-not $accountField) {
    Write-VisibleSummary 'recherche du champ identifiant'
    Write-TreeDump -Root (Get-UiaMainWindow) -Title 'fenêtre au moment de la connexion' -MaxLines 150
    Close-AppleMusic | Out-Null
    Stop-Connexion 1 "champ de l'identifiant Apple introuvable : voir l'arbre UIA ci-dessus"
}

$typed = Set-Text -Element $accountField -Text $env:AMK_APPLE_ID -Secret
Write-Host "  identifiant saisi ($typed)"
Send-Key Enter
$password = Find-Element -ControlType 'Edit' -TimeoutSeconds 20 -VisibleOnly -Where { param($e) $e.Current.IsPassword }
if (-not $password) {
    $next = Find-Element -ControlType 'Button' -NamePattern '^(Continue|Continuer|Next|Suivant|Sign In|Se connecter)$' -VisibleOnly
    if ($next) { Invoke-Click -Element $next | Out-Null }
    $password = Find-Element -ControlType 'Edit' -TimeoutSeconds 20 -VisibleOnly -Where { param($e) $e.Current.IsPassword }
}
if (-not $password) {
    Write-VisibleSummary 'recherche du champ mot de passe'
    Close-AppleMusic | Out-Null
    Stop-Connexion 1 'champ du mot de passe introuvable après la saisie de l''identifiant'
}
$typed = Set-Text -Element $password -Text $env:AMK_APPLE_PASSWORD -Secret
Write-Host "  mot de passe saisi ($typed)"
Send-Key Enter

# Waits for the verification code in the repository: .live/code-2fa-<run id>.txt on the branch of this run, committed
# by the coordinator once the owner reads the SMS. The run id in the name means an old code is never reused. The code
# is masked as soon as it is read. Returns the digits, or an empty string after the timeout.
function Wait-CodeFromRepository([int]$TimeoutMinutes) {
    if (-not $env:GITHUB_TOKEN -or -not $env:GITHUB_REPOSITORY -or -not $env:GITHUB_RUN_ID) { return '' }
    $ref = if ($env:AMK_CODE_REF) { $env:AMK_CODE_REF } else { $env:GITHUB_REF_NAME }
    $url = "https://api.github.com/repos/$($env:GITHUB_REPOSITORY)/contents/.live/code-2fa-$($env:GITHUB_RUN_ID).txt?ref=$ref"
    $headers = @{ Authorization = "Bearer $($env:GITHUB_TOKEN)"; Accept = 'application/vnd.github.raw'; 'X-GitHub-Api-Version' = '2022-11-28' }
    Write-Host "  Apple demande un code : en attente de .live/code-2fa-$($env:GITHUB_RUN_ID).txt sur $ref ($TimeoutMinutes min au plus)"
    $deadline = (Get-Date).AddMinutes($TimeoutMinutes)
    while ((Get-Date) -lt $deadline) {
        try {
            $text = Invoke-RestMethod -Uri $url -Headers $headers -ErrorAction Stop
            $digits = ([string]$text -replace '\D', '')
            if ($digits.Length -eq 6) {
                Write-Host "::add-mask::$digits"
                Register-UiaSecret $digits
                Write-Host '  code reçu par le dépôt'
                return $digits
            }
        } catch {
            # 404 until the file is committed: keep waiting.
        }
        Start-Sleep -Seconds 10
    }
    Write-Host '  aucun code reçu à temps'
    return ''
}

# Outcome: signed in, a verification code asked, or an error shown.
$codePattern = 'verification code|Verification Code|code de vérification|Two-Factor|two-factor|double authentification|Enter the code|Saisissez le code|six-digit|6-digit|6 chiffres'
$errorPattern = 'incorrect|not valid|invalid|locked|verrouill|disabled|désactivé|try again later|réessayer plus tard|could not|impossible'
$codeTyped = $false
$outcome = $null
$deadline = (Get-Date).AddSeconds(120)
while (-not $outcome -and (Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 3
    $codePrompt = Find-Element -NamePattern $codePattern -VisibleOnly
    $boxes = @(Find-Element -ControlType 'Edit' -All -VisibleOnly -Where {
        param($e) -not $e.Current.IsPassword -and $e.Current.Name -notmatch '^(Search|Rechercher)'
    })
    if (-not $codeTyped -and ($codePrompt -or $boxes.Count -ge 6)) {
        if (-not $env:AMK_CODE_2FA) {
            # No code given at launch (SMS accounts cannot make one beforehand): wait for the coordinator to commit it.
            Save-Screenshot -Ctx $ctx -Name '01b-code-demande' | Out-Null
            $env:AMK_CODE_2FA = Wait-CodeFromRepository -TimeoutMinutes 15
            if (-not $env:AMK_CODE_2FA) { $outcome = 'code-requis'; break }
            $deadline = (Get-Date).AddSeconds(120)  # the wait used up the first window: give the code its own
        }
        $digits = ($env:AMK_CODE_2FA -replace '\D', '')
        $target = $boxes | Where-Object { $_.Current.Name -match 'code|Code|digit|chiffre' } | Select-Object -First 1
        if (-not $target -and $boxes.Count -ge 1) { $target = $boxes[0] }
        if ($target) {
            Set-Text -Element $target -Text $digits -Secret -NoClear | Out-Null
        } else {
            [AmkInput]::SendText($digits, 60) | Out-Null
        }
        $codeTyped = $true
        Write-Host '  code de double authentification saisi'
        Start-Sleep -Seconds 3
        $confirm = Find-Element -ControlType 'Button' -NamePattern '^(Continue|Continuer|Verify|Vérifier|Done|OK|Terminé)$' -VisibleOnly
        if ($confirm) { Invoke-Click -Element $confirm | Out-Null }
        continue
    }
    $problem = Find-Element -ControlType 'Text' -NamePattern $errorPattern -VisibleOnly
    if ($problem) { $outcome = "erreur : $(Format-Element $problem)"; break }
    if ((Test-AppleMusicSignedIn) -eq 'oui' -and -not (Find-Element -ControlType 'Edit' -VisibleOnly -Where { param($e) $e.Current.IsPassword })) {
        $outcome = 'connecte'
    }
}

if ($outcome -eq 'code-requis') {
    Close-AppleMusic | Out-Null
    Stop-Connexion 2 ("Apple demande un code de double authentification et aucun n'a été fourni. Sur un appareil de confiance : " +
        "Réglages > [ton nom] > Connexion et sécurité > Obtenir un code de validation, puis relancer le workflow « Banc Windows connecté » " +
        "avec ce code dans le champ code_2fa (le code expire vite : lancer le workflow juste après).")
}
if ($outcome -ne 'connecte') {
    Write-VisibleSummary 'après la saisie du mot de passe'
    Close-AppleMusic | Out-Null
    $reason = if ($outcome) { $outcome } else { 'ni connexion ni demande de code après 120 s' }
    Stop-Connexion 1 "connexion échouée : $reason"
}

Start-Sleep -Seconds 8
# The welcome screen ("Welcome to Apple Music", Start Listening) comes right after sign-in: close it first, so that
# the subscription check below looks at the app and not at that screen.
for ($i = 0; $i -lt 3 -and (Close-FirstRunDialogs); $i++) { Start-Sleep -Seconds 2 }
$subscription = Find-Element -NamePattern $script:Patterns.Subscribe -VisibleOnly
Write-Host "  abonnement Apple Music : $(if ($subscription) { "offre affichée ($(Format-Element $subscription)) : le compte n'a peut-être pas d'abonnement, la lecture complète échouera" } else { 'aucune offre affichée' })"
Save-Screenshot -Ctx $ctx -Name '02-connecte' | Out-Null
Set-StepOutput 'fresh' 'true'

Close-AppleMusic | Out-Null
Stop-BenchProcesses
& (Join-Path $PSScriptRoot 'session-cache.ps1') -Mode Save -CacheDir $CacheDir
Stop-Connexion 0 'Apple Music est connecté au compte de test'
