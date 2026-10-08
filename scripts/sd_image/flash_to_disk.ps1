# 把 SD 镜像整盘写入读卡器里的卡，写完读回比对 MD5。需管理员权限。
# ⚠ 会清空目标盘。先用 Get-Disk 确认磁盘号（看 BusType / 容量），再显式传入：
#   Get-Disk
#   .\flash_to_disk.ps1 -DiskNumber <N>
# 做法同 Rufus / Etcher：写入前对该盘每个卷加锁并卸载，句柄保持到写完；分区表所在的头 4MB 最后写。
param(
    [Parameter(Mandatory = $true)][int]$DiskNumber,
    [string]$Img = "$PSScriptRoot\sd_persistent.img"
)
$ErrorActionPreference = 'Stop'
Start-Transcript -Path "$PSScriptRoot\flash_log.txt" -Force | Out-Null

Add-Type @"
using System; using System.Runtime.InteropServices; using Microsoft.Win32.SafeHandles;
public static class Vol {
  [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
  public static extern SafeFileHandle CreateFile(string n, uint acc, uint share, IntPtr sa, uint disp, uint flags, IntPtr t);
  [DllImport("kernel32.dll", SetLastError=true)]
  public static extern bool DeviceIoControl(SafeFileHandle h, uint code, IntPtr i, uint il, IntPtr o, uint ol, out uint ret, IntPtr ov);
  public const uint LOCK = 0x00090018, DISMOUNT = 0x00090020;
  public static SafeFileHandle LockDismount(string path) {
    var h = CreateFile(path, 0xC0000000, 3, IntPtr.Zero, 3, 0, IntPtr.Zero);
    if (h.IsInvalid) throw new Exception("open " + path + " err " + Marshal.GetLastWin32Error());
    uint r; bool l = DeviceIoControl(h, LOCK, IntPtr.Zero, 0, IntPtr.Zero, 0, out r, IntPtr.Zero);
    bool d = DeviceIoControl(h, DISMOUNT, IntPtr.Zero, 0, IntPtr.Zero, 0, out r, IntPtr.Zero);
    Console.WriteLine("  " + path + " lock=" + l + " dismount=" + d);
    return h;
  }
}
"@

$Chunk = 4MB
try {
    $d = Get-Disk -Number $DiskNumber
    "Target: disk $($d.Number) '$($d.FriendlyName)' bus=$($d.BusType) size=$([math]::Round($d.Size/1GB,1))GB"
    if (($d.BusType -notin 'USB', 'SD', 'MMC') -or $d.IsBoot -or $d.IsSystem -or $d.Size -gt 256GB) {
        throw "Safety check failed (只允许 USB/SD 可移动盘，非系统盘，<=256GB)，未写入。"
    }
    $imgLen = (Get-Item $Img).Length
    if ($imgLen -gt $d.Size) { throw "镜像比盘大。" }

    "Locking volumes on disk ${DiskNumber}:"
    $held = @()
    foreach ($p in Get-Partition -DiskNumber $DiskNumber -ErrorAction SilentlyContinue) {
        foreach ($vp in $p.AccessPaths) { if ($vp -like '\\?\Volume*') { $held += [Vol]::LockDismount($vp.TrimEnd('\')) } }
    }

    $dst = New-Object IO.FileStream("\\.\PhysicalDrive$DiskNumber", [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::ReadWrite)
    $src = [IO.File]::OpenRead($Img)
    $buf = New-Object byte[] $Chunk
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $off = [int64]$Chunk; $src.Position = $off
    while (($n = $src.Read($buf, 0, $Chunk)) -gt 0) {
        for ($t = 1; ; $t++) {
            try { $dst.Position = $off; $dst.Write($buf, 0, $n); break }
            catch { if ($t -ge 20) { throw }; "retry $t at $([int]($off/1MB)) MB"; Start-Sleep -Milliseconds 1500 }
        }
        $off += $n
        if (($off % 512MB) -eq 0) { "written $([int]($off/1MB)) MB  ($([int]$sw.Elapsed.TotalSeconds)s)" }
    }
    $src.Position = 0; $n = $src.Read($buf, 0, $Chunk)
    $dst.Position = 0; $dst.Write($buf, 0, $n)
    $dst.Flush(); "write done in $([int]$sw.Elapsed.TotalSeconds)s"

    $md5a = [Security.Cryptography.MD5]::Create(); $md5b = [Security.Cryptography.MD5]::Create()
    $b2 = New-Object byte[] $Chunk; $src.Position = 0; $dst.Position = 0; $left = $imgLen
    while ($left -gt 0) {
        $n = $src.Read($buf, 0, $Chunk); $m = 0
        while ($m -lt $n) { $m += $dst.Read($b2, $m, $n - $m) }
        [void]$md5a.TransformBlock($buf, 0, $n, $null, 0); [void]$md5b.TransformBlock($b2, 0, $n, $null, 0); $left -= $n
    }
    [void]$md5a.TransformFinalBlock(@(), 0, 0); [void]$md5b.TransformFinalBlock(@(), 0, 0)
    $src.Close(); $dst.Close()
    foreach ($h in $held) { $h.Close() }
    $ha = [BitConverter]::ToString($md5a.Hash); $hb = [BitConverter]::ToString($md5b.Hash)
    "image md5 = $ha"; "disk  md5 = $hb"
    if ($ha -eq $hb) { "VERIFY OK" } else { "VERIFY FAILED" }
    Update-HostStorageCache
}
catch { "ERROR: $_" }
finally { Stop-Transcript | Out-Null }
