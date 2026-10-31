# Loads the live bench helpers before its cases (run-bench.ps1 -CasesDir live\cases loads the files in name order).
# No case here: UI Automation, Core Audio checks and the shared live helpers.

. (Join-Path $PSScriptRoot '..\..\lib\uia.ps1')
. (Join-Path $PSScriptRoot '..\..\lib\audio.ps1')
. (Join-Path $PSScriptRoot '..\live-common.ps1')

# Storefront of the test account for the music:// links (fr by default), from the workflow input.
$script:LiveStorefront = if ($env:AMPIFY_STOREFRONT) { $env:AMPIFY_STOREFRONT } else { 'fr' }
