<#
.SYNOPSIS
Signs Apple Music in to the test account on the runner, through UI Automation, without Ampify.

.DESCRIPTION
1. Installs Apple Music (winget) if it is missing, then restores the encrypted session cache if there is one.
2. Starts Apple Music normally (without the launcher or the kernel: Ampify is never present during the sign-in).
3. Already signed in: nothing to do. Otherwise, with -AllowSignIn: sign-in button, Apple ID and password
   (variables AMPIFY_APPLE_ID and AMPIFY_APPLE_PASSWORD), then the two-factor code (AMPIFY_CODE_2FA) if Apple asks
   for it. Without a code when Apple asks for one: immediate stop with a clear message (exit code 2).
4. After a new sign-in: closes Apple Music and writes the encrypted session cache.
The credentials are never shown or written. No screenshot between typing the identifier and the confirmed
sign-in (the identifier would be visible).

.OUTPUTS
Code 0: signed in. 1: failure. 2: two-factor code needed and missing. 3: not signed in, and signing in with a
password is not allowed (-AllowSignIn missing, for example on a push).
#>
[CmdletBinding()]
param(
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\..\..\results-live'),
    [string]$CacheDir = (Join-Path ([IO.Path]::GetTempPath()) 'ampify-session'),
    [switch]$AllowSignIn
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\lib\harness.ps1')
. (Join-Path $PSScriptRoot '..\lib\uia.ps1')
. (Join-Path $PSScriptRoot 'live-common.ps1')

$dir = Join-Path $OutputRoot 'sign-in'
New-Item -ItemType Directory -Force -Path $dir | Out-Null
$ctx = [pscustomobject]@{ Id = 'sign-in'; Dir = $dir }

function Set-StepOutput([string]$Name, [string]$Value) {
    if ($env:GITHUB_OUTPUT) { "$Name=$Value" | Add-Content -LiteralPath $env:GITHUB_OUTPUT -Encoding utf8 }
}

function Stop-SignIn([int]$Code, [string]$Message) {
    if ($Code -eq 0) { Write-Host "==> $Message" } else { Write-Host "::error::$(Protect-UiaText $Message)" }
    Set-StepOutput 'signed_in' $(if ($Code -eq 0) { 'true' } else { 'false' })
    exit $Code
}

Write-Host '==> Signing in to Apple Music (without Ampify)'
$state = Install-AppleMusic -LogFile (Join-Path $dir 'winget.log')
Write-Host "  Apple Music: $state"
if (-not (Get-AppleMusicFullName)) { Stop-SignIn 1 "Apple Music is not installed on this runner ($state)" }

Stop-BenchProcesses
& (Join-Path $PSScriptRoot 'session-cache.ps1') -Mode Restore -CacheDir $CacheDir

Start-AppNormally
if (-not (Wait-AppleMusicReady -TimeoutSeconds 120)) {
    Save-Screenshot -Ctx $ctx -Name '01-startup' | Out-Null
    Write-VisibleSummary 'startup'
    Stop-SignIn 1 "Apple Music does not show its main window after 120 s"
}
Show-AppleMusicWindow | Out-Null
Start-Sleep -Seconds 5

$signed = Test-AppleMusicSignedIn
Write-Host "  already signed in: $signed"
if ($signed -eq 'yes') {
    Save-Screenshot -Ctx $ctx -Name '02-signed-in' | Out-Null
    Set-StepOutput 'fresh' 'false'
    Close-AppleMusic | Out-Null
    Stop-SignIn 0 'Apple Music is already signed in (session restored)'
}
if (-not $AllowSignIn) {
    Close-AppleMusic | Out-Null
    Stop-SignIn 3 "Apple Music is not signed in and signing in with a password is only allowed on a manual run (-AllowSignIn)"
}
if (-not $env:AMPIFY_APPLE_ID -or -not $env:AMPIFY_APPLE_PASSWORD) {
    Close-AppleMusic | Out-Null
    Stop-SignIn 1 'secrets AMPIFY_APPLE_ID and AMPIFY_APPLE_PASSWORD missing: see docs/development/bench-and-ci.md'
}

# Before typing anything: the empty sign-in screen can be captured.
Save-Screenshot -Ctx $ctx -Name '01-before-sign-in' | Out-Null

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
        Write-Host "  click: $(Format-Element $button) ($(Invoke-Click -Element $button))"
    }
    Start-Sleep -Seconds 4
}
if (-not $accountField) {
    Write-VisibleSummary 'looking for the identifier field'
    Write-TreeDump -Root (Get-UiaMainWindow) -Title 'window at sign-in time' -MaxLines 150
    Close-AppleMusic | Out-Null
    Stop-SignIn 1 "Apple ID field not found: see the UIA tree above"
}

$typed = Set-Text -Element $accountField -Text $env:AMPIFY_APPLE_ID -Secret
Write-Host "  identifier typed ($typed)"
Send-Key Enter
$password = Find-Element -ControlType 'Edit' -TimeoutSeconds 20 -VisibleOnly -Where { param($e) $e.Current.IsPassword }
if (-not $password) {
    $next = Find-Element -ControlType 'Button' -NamePattern '^(Continue|Continuer|Next|Suivant|Sign In|Se connecter)$' -VisibleOnly
    if ($next) { Invoke-Click -Element $next | Out-Null }
    $password = Find-Element -ControlType 'Edit' -TimeoutSeconds 20 -VisibleOnly -Where { param($e) $e.Current.IsPassword }
}
if (-not $password) {
    Write-VisibleSummary 'looking for the password field'
    Close-AppleMusic | Out-Null
    Stop-SignIn 1 'password field not found after typing the identifier'
}
$typed = Set-Text -Element $password -Text $env:AMPIFY_APPLE_PASSWORD -Secret
Write-Host "  password typed ($typed)"
Send-Key Enter

# Waits for the verification code in the repository: .live/code-2fa-<run id>.txt on the branch of this run, committed
# by the person running the bench once they read the SMS. The run id in the name means an old code is never reused.
# The code is masked as soon as it is read. Returns the digits, or an empty string after the timeout.
function Wait-CodeFromRepository([int]$TimeoutMinutes) {
    if (-not $env:GITHUB_TOKEN -or -not $env:GITHUB_REPOSITORY -or -not $env:GITHUB_RUN_ID) { return '' }
    $ref = if ($env:AMPIFY_CODE_REF) { $env:AMPIFY_CODE_REF } else { $env:GITHUB_REF_NAME }
    $url = "https://api.github.com/repos/$($env:GITHUB_REPOSITORY)/contents/.live/code-2fa-$($env:GITHUB_RUN_ID).txt?ref=$ref"
    $headers = @{ Authorization = "Bearer $($env:GITHUB_TOKEN)"; Accept = 'application/vnd.github.raw'; 'X-GitHub-Api-Version' = '2022-11-28' }
    Write-Host "  Apple asks for a code: waiting for .live/code-2fa-$($env:GITHUB_RUN_ID).txt on $ref ($TimeoutMinutes min at most)"
    $deadline = (Get-Date).AddMinutes($TimeoutMinutes)
    while ((Get-Date) -lt $deadline) {
        try {
            $text = Invoke-RestMethod -Uri $url -Headers $headers -ErrorAction Stop
            $digits = ([string]$text -replace '\D', '')
            if ($digits.Length -eq 6) {
                Write-Host "::add-mask::$digits"
                Register-UiaSecret $digits
                Write-Host '  code received through the repository'
                return $digits
            }
        } catch {
            # 404 until the file is committed: keep waiting.
        }
        Start-Sleep -Seconds 10
    }
    Write-Host '  no code received in time'
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
        if (-not $env:AMPIFY_CODE_2FA) {
            # No code given at launch (SMS accounts cannot make one beforehand): wait for it to be committed.
            Save-Screenshot -Ctx $ctx -Name '01b-code-asked' | Out-Null
            $env:AMPIFY_CODE_2FA = Wait-CodeFromRepository -TimeoutMinutes 15
            if (-not $env:AMPIFY_CODE_2FA) { $outcome = 'code-needed'; break }
            $deadline = (Get-Date).AddSeconds(120)  # the wait used up the first window: give the code its own
        }
        $digits = ($env:AMPIFY_CODE_2FA -replace '\D', '')
        $target = $boxes | Where-Object { $_.Current.Name -match 'code|Code|digit|chiffre' } | Select-Object -First 1
        if (-not $target -and $boxes.Count -ge 1) { $target = $boxes[0] }
        if ($target) {
            Set-Text -Element $target -Text $digits -Secret -NoClear | Out-Null
        } else {
            [AmpifyInput]::SendText($digits, 60) | Out-Null
        }
        $codeTyped = $true
        Write-Host '  two-factor code typed'
        Start-Sleep -Seconds 3
        $confirm = Find-Element -ControlType 'Button' -NamePattern '^(Continue|Continuer|Verify|Vérifier|Done|OK|Terminé)$' -VisibleOnly
        if ($confirm) { Invoke-Click -Element $confirm | Out-Null }
        continue
    }
    $problem = Find-Element -ControlType 'Text' -NamePattern $errorPattern -VisibleOnly
    if ($problem) { $outcome = "error: $(Format-Element $problem)"; break }
    if ((Test-AppleMusicSignedIn) -eq 'yes' -and -not (Find-Element -ControlType 'Edit' -VisibleOnly -Where { param($e) $e.Current.IsPassword })) {
        $outcome = 'signed-in'
    }
}

if ($outcome -eq 'code-needed') {
    Close-AppleMusic | Out-Null
    Stop-SignIn 2 ("Apple asks for a two-factor code and none was given. On a trusted device: " +
        "Settings > [your name] > Sign-In & Security > Get Verification Code, then run the signed-in bench again " +
        "with that code in AMPIFY_CODE_2FA (the code expires quickly: run it right after).")
}
if ($outcome -ne 'signed-in') {
    Write-VisibleSummary 'after typing the password'
    Close-AppleMusic | Out-Null
    $reason = if ($outcome) { $outcome } else { 'neither a sign-in nor a code request after 120 s' }
    Stop-SignIn 1 "sign-in failed: $reason"
}

Start-Sleep -Seconds 8
# The welcome screen ("Welcome to Apple Music", Start Listening) comes right after sign-in: close it first, so that
# the subscription check below looks at the app and not at that screen.
for ($i = 0; $i -lt 3 -and (Close-FirstRunDialogs); $i++) { Start-Sleep -Seconds 2 }
$subscription = Find-Element -NamePattern $script:Patterns.Subscribe -VisibleOnly
Write-Host "  Apple Music subscription: $(if ($subscription) { "offer shown ($(Format-Element $subscription)): the account may have no subscription, full playback will fail" } else { 'no offer shown' })"
Save-Screenshot -Ctx $ctx -Name '02-signed-in' | Out-Null
Set-StepOutput 'fresh' 'true'

Close-AppleMusic | Out-Null
Stop-BenchProcesses
& (Join-Path $PSScriptRoot 'session-cache.ps1') -Mode Save -CacheDir $CacheDir
Stop-SignIn 0 'Apple Music is signed in to the test account'
