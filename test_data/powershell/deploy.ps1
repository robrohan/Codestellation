# Entry point: every PowerShell dependency style in one script.
. .\lib\common.ps1                               # dot-source         -> lib/common.ps1
. "$PSScriptRoot\lib\helpers.ps1"                # $PSScriptRoot      -> lib/helpers.ps1
Import-Module "$PSScriptRoot\modules\Deploy"     # module folder      -> modules/Deploy/Deploy.psd1
Import-Module Tools                              # by module name     -> modules/Tools.psm1
Import-Module Az.Accounts                        # installed module: unresolved
& .\scripts\build.ps1 -Release                   # call operator      -> scripts/build.ps1
.\scripts\test.ps1                               # run directly       -> scripts/test.ps1
cmd /c scripts\legacy.bat                        # PowerShell -> batch: scripts/legacy.bat

Write-Log "deployed"
