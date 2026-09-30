using module ..\modules\Types.psm1                # -> modules/Types.psm1
param([switch]$Release)
[BuildResult]@{ Ok = $true; Log = "built" }
