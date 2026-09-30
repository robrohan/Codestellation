function Invoke-Deploy { param([string]$Target) Write-Log "deploying to $Target" }
Export-ModuleMember -Function Invoke-Deploy
