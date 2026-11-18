# 把读卡器里的整张卡（或到最后一个分区结束为止）读成镜像文件，可选 gzip 压缩；只读，不改卡。需管理员权限。
# 配合 flash_to_disk.ps1（写卡）用：先 list 认准磁盘号，再 read。
#   .\read_disk_to_image.ps1 -List
#   .\read_disk_to_image.ps1 -DiskNumber <N> -Out C:\SD备份\主卡.img               # 读到最后一个分区结束（MBR）
#   .\read_disk_to_image.ps1 -DiskNumber <N> -Out C:\SD备份\旧卡.img.gz -Whole -Gzip   # 整张卡，压缩
# 结束时打印镜像的 MD5（未压缩数据的），写进同名 .md5.txt。
param(
    [switch]$List,
    [int]$DiskNumber = -1,
    [string]$Out,
    [switch]$Whole,     # 整张卡；默认只读到 MBR 里最后一个分区的结尾（剩下的未分区空间不读）
    [switch]$Gzip,      # 输出 gzip 压缩（空白区压得很小）；flash_to_disk.ps1 只吃未压缩的 .img
    [double]$MaxOutGB = 0   # > 0：输出文件超过这么大就停（防止把盘写满）；同时跳过开始前的空间估算
)
$ErrorActionPreference = 'Stop'
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) { Write-Host '要用「以管理员身份运行」的 PowerShell。'; exit 1 }

if ($List -or $DiskNumber -lt 0) {
    Get-Disk | Select-Object Number, FriendlyName, BusType, @{n = 'SizeGB'; e = { [math]::Round($_.Size / 1GB, 1) } },
        PartitionStyle, IsBoot, IsSystem | Format-Table -AutoSize
    Write-Host '卡一般是 BusType = USB / SD / MMC、容量 8~64 GB 的那一个；IsBoot / IsSystem 为 True 的是电脑硬盘，别选。'
    exit 0
}
if (-not $Out) { Write-Host '要给 -Out <镜像文件路径>'; exit 1 }

$d = Get-Disk -Number $DiskNumber
"Source: disk $($d.Number) '$($d.FriendlyName)' bus=$($d.BusType) size=$([math]::Round($d.Size/1GB,1))GB style=$($d.PartitionStyle)"
if (($d.BusType -notin 'USB', 'SD', 'MMC') -or $d.IsBoot -or $d.IsSystem -or $d.Size -gt 256GB) {
    throw '安全检查不过（只读 USB/SD 可移动盘，非系统盘，<=256GB）。'
}
$src = New-Object IO.FileStream("\\.\PhysicalDrive$DiskNumber", [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
$len = [int64]$d.Size
if (-not $Whole) {
    $mbr = New-Object byte[] 512
    [void]$src.Read($mbr, 0, 512)
    if ($mbr[510] -ne 0x55 -or $mbr[511] -ne 0xAA) { throw 'MBR 签名不对：用 -Whole 读整张卡。' }
    $end = [int64]0
    for ($k = 0; $k -lt 4; $k++) {
        $e = 446 + 16 * $k
        if ($mbr[$e + 4] -eq 0xEE) { throw 'GPT 分区表：用 -Whole 读整张卡。' }
        $start = [BitConverter]::ToUInt32($mbr, $e + 8); $cnt = [BitConverter]::ToUInt32($mbr, $e + 12)
        if ($cnt -gt 0) {
            "  partition $($k+1): type 0x$('{0:X2}' -f $mbr[$e+4]) start $start size $cnt sectors ($([math]::Round($cnt*512/1MB)) MB)"
            $end = [math]::Max($end, ([int64]$start + $cnt) * 512)
        }
    }
    if ($end -le 0) { throw 'MBR 里没有分区：用 -Whole。' }
    $len = [math]::Min([int64]$len, [int64]($end + 1MB))   # +1MB margin
    $src.Position = 0
}
$dir = Split-Path -Parent $Out
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
$free = (Get-PSDrive -Name ($Out.Substring(0, 1))).Free
$need = if ($Gzip) { [int64]($len * 0.6) } else { $len }
"Reading $([math]::Round($len/1GB,2)) GB -> $Out （目标盘剩余 $([math]::Round($free/1GB,1)) GB）"
if ($MaxOutGB -gt 0) { if ($MaxOutGB * 1GB -gt $free - 2GB) { throw "-MaxOutGB 比目标盘剩余空间（减 2 GB）还大。" } }
elseif ($need -gt $free) { throw "目标盘空间可能不够（估计要 $([math]::Round($need/1GB,1)) GB）。换个盘，或加 -Gzip。" }

$fs = [IO.File]::Create($Out)
$dst = if ($Gzip) { New-Object IO.Compression.GZipStream($fs, [IO.Compression.CompressionLevel]::Fastest) } else { $fs }
$md5 = [Security.Cryptography.MD5]::Create()
$buf = New-Object byte[] 4MB
$done = [int64]0
$sw = [Diagnostics.Stopwatch]::StartNew()
try {
    while ($done -lt $len) {
        $want = [int][math]::Min([int64]$buf.Length, [int64]($len - $done))
        $n = $src.Read($buf, 0, $want)
        if ($n -le 0) { break }
        $dst.Write($buf, 0, $n)
        [void]$md5.TransformBlock($buf, 0, $n, $null, 0)
        $done += $n
        if ($MaxOutGB -gt 0 -and $fs.Length -gt $MaxOutGB * 1GB) { throw "输出已超过 $MaxOutGB GB 上限，停止（读到 $([int]($done/1MB)) MB）。" }
        if (($done % 512MB) -lt $n) { "  read $([int]($done/1MB)) / $([int]($len/1MB)) MB  ($([int]$sw.Elapsed.TotalSeconds)s)" }
    }
    [void]$md5.TransformFinalBlock($buf, 0, 0)
} finally {
    $dst.Dispose(); if ($Gzip) { $fs.Dispose() }; $src.Dispose()
}
$hex = ([BitConverter]::ToString($md5.Hash) -replace '-', '').ToLower()
"$hex  $([int64]$done) bytes" | Set-Content "$Out.md5.txt"
"Done: $([math]::Round($done/1GB,2)) GB in $([int]$sw.Elapsed.TotalSeconds)s, MD5 (raw) $hex -> $Out.md5.txt"
