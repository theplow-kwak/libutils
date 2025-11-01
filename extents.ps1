param (
    [string]$folder
)

Get-ChildItem -Path $folder -Recurse | ForEach-Object {
    if ($_.Length -ge 100MB) {
        # Code to execute for files with size 1GB or more
        $fsutilOutput = fsutil file queryextents $($_.FullName) 2>&1
        $extentsLines = $fsutilOutput | Where-Object { $_ -match 'VCN:\s0x[0-9a-fA-F]+' }
        $extentsCount = $extentsLines.Count

        if ($extentsCount -gt 1) {
            Write-Output "File $($_.FullName) has a size of $([math]::Round($_.Length / 1GB, 2)) GB"
            Write-Output " Extents : $extentsCount"
            # Output only the first 5 lines of fsutilOutput
            Write-Output ($fsutilOutput | Select-Object -First 5)
            Write-Output " "
        }
    }
}
