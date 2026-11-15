#!/usr/bin/env python3
"""把启动分区 system.dtb 里的 HDMI 像素时钟从 150 MHz 改成标准的 148.5 MHz（原地改 6 个字节）。

厂家设备树的 display-timings/native-mode 指向 timing1（1920x1080、clock-frequency 150000000），
axi_dynclk 的 expclk 也是 150000000：内核 anlogic-dynclk 驱动每次开机都把像素时钟 PLL 配成 150 MHz，
行场总数 2200 x 1125 不变，实际输出 60.61 Hz，像素时钟比 CEA-861 的 148.5 MHz 高 1.0%（容差 ±0.5%），
挑时序的显示器会偶发失锁（整屏黑一下、重新识别信号源）。与 PL 比特流无关。

用法：python patch_dtb_pixclk.py <原 system.dtb> <输出文件>
上板：启动分区先留原文件备份（system.dtb.orig），再换上输出文件并重启；
      dmesg | grep dynclk 应显示 output=148500000 Hz、actual 约 148498535 Hz。
"""
import hashlib
import struct
import sys

OLD, NEW = 150000000, 148500000
TARGETS = {('/soc/axi_dynclk@80101000', 'expclk'),
           ('/soc/vdmafb@80100000/display-timings/timing1', 'clock-frequency')}


def props(d):
    magic, _total, off_struct, off_strings = struct.unpack('>4I', d[:16])
    if magic != 0xd00dfeed:
        raise SystemExit('不是设备树文件（magic 0x%08x）' % magic)
    p, stack = off_struct, []
    while True:
        tok, = struct.unpack('>I', d[p:p + 4]); p += 4
        if tok == 1:
            e = d.index(b'\0', p); stack.append(d[p:e].decode()); p = (e + 4) & ~3
        elif tok == 2:
            stack.pop()
        elif tok == 3:
            ln, no = struct.unpack('>II', d[p:p + 8]); p += 8
            e = d.index(b'\0', off_strings + no)
            yield '/'.join(stack), d[off_strings + no:e].decode(), p, ln
            p = (p + ln + 3) & ~3
        elif tok == 9:
            return
        elif tok != 4:
            raise SystemExit('设备树结构解析失败（偏移 0x%x）' % p)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    d = bytearray(open(sys.argv[1], 'rb').read())
    done = 0
    for path, name, off, ln in props(bytes(d)):
        if (path, name) in TARGETS:
            if ln != 4 or struct.unpack('>I', d[off:off + 4])[0] != OLD:
                raise SystemExit('%s/%s 不是 %d：这份设备树和预期的不一样，没有改' % (path, name, OLD))
            d[off:off + 4] = struct.pack('>I', NEW); done += 1
    if done != len(TARGETS):
        raise SystemExit('只找到 %d / %d 处：没有输出' % (done, len(TARGETS)))
    open(sys.argv[2], 'wb').write(bytes(d))
    print('已改 %d 处（%d -> %d），md5 %s' % (done, OLD, NEW, hashlib.md5(bytes(d)).hexdigest()))


if __name__ == '__main__':
    main()
