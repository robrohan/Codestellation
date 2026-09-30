function Get-Tool([string]$Name) { Get-Command $Name -ErrorAction SilentlyContinue }
