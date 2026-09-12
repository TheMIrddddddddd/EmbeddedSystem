$ErrorActionPreference = 'Stop'

$logPath = Join-Path $PSScriptRoot 'format_tfcard.log'
Start-Transcript -Path $logPath -Force | Out-Null

try
{
    $diskNumber = 2
    $expectedSize = 63864569856

    $disk = Get-Disk -Number $diskNumber
    if ($null -eq $disk)
    {
        throw "Disk $diskNumber was not found."
    }
    if (($disk.BusType -ne 'USB') -or $disk.IsBoot -or $disk.IsSystem)
    {
        throw "Disk $diskNumber is not the verified removable TF card."
    }
    if ($disk.Size -ne $expectedSize)
    {
        throw "Disk $diskNumber size changed: $($disk.Size)."
    }

    "TARGET Disk=$diskNumber Name=$($disk.FriendlyName) Bus=$($disk.BusType) Size=$($disk.Size)"

    Set-Disk -Number $diskNumber -IsReadOnly $false -ErrorAction SilentlyContinue
    Set-Disk -Number $diskNumber -IsOffline $false -ErrorAction SilentlyContinue
    Clear-Disk -Number $diskNumber -RemoveData -RemoveOEM -Confirm:$false
    Set-Disk -Number $diskNumber -IsOffline $false -ErrorAction SilentlyContinue
    $partitionStyle = (Get-Disk -Number $diskNumber).PartitionStyle
    if ($partitionStyle -eq 'RAW')
    {
        Initialize-Disk -Number $diskNumber -PartitionStyle MBR -Confirm:$false
    }
    elseif ($partitionStyle -ne 'MBR')
    {
        Set-Disk -Number $diskNumber -PartitionStyle MBR -Confirm:$false
    }
    $partition = New-Partition -DiskNumber $diskNumber -UseMaximumSize -DriveLetter G -ErrorAction Stop
    Format-Volume -Partition $partition -FileSystem exFAT -NewFileSystemLabel 'TFCARD' -Confirm:$false -Force

    Get-Disk -Number $diskNumber |
        Select-Object Number, FriendlyName, BusType, PartitionStyle, OperationalStatus, Size, IsReadOnly |
        Format-List
    Get-Partition -DiskNumber $diskNumber |
        Select-Object DiskNumber, PartitionNumber, DriveLetter, Type, Size, Offset |
        Format-List
    Get-Volume -DriveLetter G |
        Select-Object DriveLetter, FileSystemLabel, FileSystem, HealthStatus, Size, SizeRemaining, DriveType |
        Format-List

    "RESULT=OK"
}
catch
{
    "RESULT=FAIL: $($_.Exception.Message)"
}
finally
{
    Stop-Transcript | Out-Null
}
