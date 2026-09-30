# A PowerShell script run from build.bat.
Compress-Archive -Path out\* -DestinationPath dist.zip -Force
