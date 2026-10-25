#!/usr/bin/env python3
"""产线 PLC 模拟器（PC + USB-CAN）—— DragonEyeSorter 的 CAN 外设一侧（赛题硬性项⑤）。

下发：启动 / 停止 / 暂停 / 恢复 / 复位报警、带速、配方、心跳（2 Hz）、对时（连上时 + 每 60 s，板子无 RTC）。
显示：板子状态（10 Hz）、计数与良率、逐件结果（类别 / 去向 / 置信度 / 尺寸）、报警、日志。
统计（测试报告「总线稳定性」）：收发帧数、命令 ACK 往返时间（平均 / 最大）、STATUS 间隔超过 150 ms 的次数。

    python plc_gui.py --backend virtual                     # 没有硬件：进程内模拟板子
    python plc_gui.py --backend python-can --iface socketcan --channel can0
    python plc_gui.py --backend zqwl [--port COM9]          # ZQWL-UCAN-101E（串口协议，免驱动 / DLL）
"""
import argparse
import json
import os
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk

import can_proto as P
from backends import open_bus

HERE = os.path.dirname(os.path.abspath(__file__))
STATE_COLORS = {"IDLE": "#9e9e9e", "RUN": "#2e7d32", "PAUSED": "#f9a825", "ESTOP": "#c62828", "FAULT": "#c62828"}


def load_names():
    labels, bins, outlets = [], ["gear_box", "rack_box"], ["recheck", "scrap", "rework"]
    try:
        with open(os.path.join(HERE, "..", "..", "models", "label_list.json"), encoding="utf-8") as f:
            labels = [l.get("cn", l["label"]) for l in sorted(json.load(f)["labels"], key=lambda l: l["id"])]
        with open(os.path.join(HERE, "..", "..", "ps", "src", "des_app.json"), encoding="utf-8") as f:
            cfg = json.load(f)
            bins, outlets = cfg.get("bins", bins), cfg.get("outlets", outlets)
    except (OSError, ValueError, KeyError):
        pass
    return labels, bins, outlets


class Link:
    """bus I/O thread: decoded frames -> queue; sends with sequence numbers and ACK timing"""

    def __init__(self, bus):
        self.bus = bus
        self.q = queue.Queue()
        self.seq = 0
        self.pending = {}            # (cmd_id, seq) -> (send time, description)
        self.tx = self.rx = 0
        self.rtt = []
        self.status_gaps = 0
        self.last_status = None
        self.lock = threading.Lock()
        self.quit = False
        threading.Thread(target=self._rx_loop, daemon=True).start()

    def send(self, frame, desc=None):
        arb, data = frame
        with self.lock:
            if desc is not None:
                self.pending[(arb, data[-1])] = (time.perf_counter(), desc)
            self.bus.send(arb, data)
            self.tx += 1

    def next_seq(self):
        self.seq = (self.seq + 1) & 0xFF
        return self.seq

    def _rx_loop(self):
        while not self.quit:
            fr = self.bus.recv(0.05)
            if fr is None:
                continue
            self.rx += 1
            kind, v = P.decode(*fr)
            if kind is None:
                continue
            if kind == "ack":
                with self.lock:
                    p = self.pending.pop((v["cmd_id"], v["seq"]), None)
                if p:
                    ms = (time.perf_counter() - p[0]) * 1000
                    self.rtt.append(ms)
                    v["rtt_ms"], v["desc"] = ms, p[1]
            if kind == "status":
                now = time.perf_counter()
                if self.last_status is not None and now - self.last_status > 0.15:
                    self.status_gaps += 1
                self.last_status = now
            self.q.put((kind, v))


class App:
    def __init__(self, root, link, names):
        self.root, self.link = root, link
        self.labels, self.bins, self.outlets = names
        self.hb = 0
        self.last_time_sync = 0.0
        root.title("产线 PLC 模拟器 · DragonEyeSorter (CAN 500 kbit/s)")
        root.geometry("1100x720")
        style = ttk.Style()
        style.configure("Big.TLabel", font=("Microsoft YaHei UI", 20, "bold"))

        top = ttk.Frame(root, padding=8)
        top.pack(fill="x")
        self.state_lbl = tk.Label(top, text="未连接", width=10, font=("Microsoft YaHei UI", 20, "bold"), fg="white",
                                  bg="#616161")
        self.state_lbl.pack(side="left", padx=(0, 12))
        self.info = ttk.Label(top, text="", font=("Microsoft YaHei UI", 11))
        self.info.pack(side="left")

        ctl = ttk.LabelFrame(root, text="控制", padding=8)
        ctl.pack(fill="x", padx=8)
        for text, c in (("启动", P.CMD_START), ("停止", P.CMD_STOP), ("暂停（联锁）", P.CMD_PAUSE), ("恢复", P.CMD_RESUME),
                        ("复位报警", P.CMD_RESET_ALARM)):
            ttk.Button(ctl, text=text, command=lambda c=c: self.cmd(c)).pack(side="left", padx=4)
        ttk.Label(ctl, text="   带速 mm/s").pack(side="left")
        self.speed = tk.DoubleVar(value=50.0)
        ttk.Spinbox(ctl, from_=5, to=200, increment=5, textvariable=self.speed, width=6).pack(side="left", padx=4)
        ttk.Button(ctl, text="设定", command=self.set_speed).pack(side="left")
        ttk.Label(ctl, text="   配方").pack(side="left")
        self.recipe = tk.IntVar(value=1)
        ttk.Spinbox(ctl, from_=1, to=255, textvariable=self.recipe, width=4).pack(side="left", padx=4)
        ttk.Button(ctl, text="下发", command=self.set_recipe).pack(side="left")
        self.hb_on = tk.BooleanVar(value=True)
        ttk.Checkbutton(ctl, text="心跳 2 Hz", variable=self.hb_on).pack(side="left", padx=12)
        ttk.Button(ctl, text="对时", command=self.time_sync).pack(side="left")

        mid = ttk.Frame(root, padding=8)
        mid.pack(fill="both", expand=True)
        left = ttk.Frame(mid)
        left.pack(side="left", fill="y")
        cnt = ttk.LabelFrame(left, text="计数", padding=8)
        cnt.pack(fill="x")
        self.count_lbl = ttk.Label(cnt, text="—", font=("Consolas", 13), justify="left")
        self.count_lbl.pack(anchor="w")
        st = ttk.LabelFrame(left, text="总线统计（测试报告）", padding=8)
        st.pack(fill="x", pady=8)
        self.stat_lbl = ttk.Label(st, text="—", font=("Consolas", 10), justify="left")
        self.stat_lbl.pack(anchor="w")

        right = ttk.LabelFrame(mid, text="逐件结果（PART 0x183）", padding=4)
        right.pack(side="left", fill="both", expand=True, padx=(8, 0))
        cols = ("id", "cls", "disp", "dest", "conf", "size", "time")
        self.tree = ttk.Treeview(right, columns=cols, show="headings", height=14)
        for c, t, w in zip(cols, ("件号", "类别", "去向", "料盒 / 出口", "置信度", "尺寸 mm", "时间"),
                           (60, 150, 70, 110, 70, 80, 90)):
            self.tree.heading(c, text=t)
            self.tree.column(c, width=w, anchor="center")
        self.tree.tag_configure("ng", foreground="#c62828")
        self.tree.tag_configure("rc", foreground="#ef6c00")
        self.tree.pack(fill="both", expand=True)

        logf = ttk.LabelFrame(root, text="日志", padding=4)
        logf.pack(fill="both", padx=8, pady=(0, 8))
        self.log = tk.Text(logf, height=8, font=("Consolas", 10))
        self.log.pack(fill="both", expand=True)

        self.time_sync()
        self.root.after(50, self.poll)
        self.root.after(500, self.heartbeat)

    def say(self, s):
        self.log.insert("end", time.strftime("%H:%M:%S ") + s + "\n")
        self.log.see("end")

    def cmd(self, c):
        seq = self.link.next_seq()
        self.link.send(P.cmd(c, seq), f"命令 {P.CMD_NAMES[c]}")

    def set_speed(self):
        seq = self.link.next_seq()
        self.link.send(P.speed(self.speed.get(), seq), f"带速 {self.speed.get():.1f} mm/s")

    def set_recipe(self):
        seq = self.link.next_seq()
        self.link.send(P.recipe(self.recipe.get(), seq), f"配方 {self.recipe.get()}")

    def time_sync(self):
        self.link.send(P.time_frame())
        self.last_time_sync = time.time()
        self.say("对时：已下发 PC 时间（TIME 0x104）")

    def heartbeat(self):
        if self.hb_on.get():
            self.hb = (self.hb + 1) & 0xFF
            self.link.send(P.plc_hb(self.hb))
        if time.time() - self.last_time_sync > 60:
            self.time_sync()
        self.root.after(500, self.heartbeat)

    def poll(self):
        try:
            while True:
                kind, v = self.link.q.get_nowait()
                getattr(self, "on_" + kind)(v)
        except queue.Empty:
            pass
        r = self.link.rtt
        self.stat_lbl.config(text=(
            f"发送 {self.link.tx}   接收 {self.link.rx}\n"
            f"ACK 往返 {len(r)} 次：平均 {sum(r) / len(r):.2f} ms  最大 {max(r):.2f} ms\n" if r else
            f"发送 {self.link.tx}   接收 {self.link.rx}\nACK 往返：—\n") +
            f"STATUS 间隔 >150ms：{self.link.status_gaps} 次\n未应答命令：{len(self.link.pending)}" +
            (f"\n适配器：{self.link.bus.status()}" if hasattr(self.link.bus, "status") else ""))
        self.root.after(50, self.poll)

    def on_status(self, v):
        name = v["state_name"]
        self.state_lbl.config(text=name, bg=STATE_COLORS.get(name, "#616161"))
        f = v["flags"]
        flags = " ".join(n for b, n in ((P.FLAG_BELT_MOVING, "皮带运行"), (P.FLAG_ARM_BUSY, "机械臂忙"),
                                        (P.FLAG_ALARM, "报警"), (P.FLAG_PLC_LINK, "PLC 链路正常"),
                                        (P.FLAG_CALIBRATED, "已标定"), (P.FLAG_RS485, "RS485 在线")) if f & b)
        self.info.config(text=f"带速 {v['speed']:.1f} mm/s   运行 {v['uptime_s']} s   报警码 {v['alarm']}   {flags}")

    def on_counts(self, v):
        tot = v["total"]
        y = 100.0 * v["ok"] / tot if tot else 0.0
        self.count_lbl.config(text=f"总数  {tot:6d}\n良品  {v['ok']:6d}\n NG   {v['ng']:6d}\n复检  {v['recheck']:6d}\n良率  {y:5.1f} %")

    def on_part(self, v):
        cls = self.labels[v["cls"]] if v["cls"] < len(self.labels) else ("未分类" if v["cls"] == 255 else str(v["cls"]))
        names = self.bins if v["disp"] == 0 else self.outlets
        dest = names[v["dest"]] if v["dest"] < len(names) else str(v["dest"])
        tag = () if v["disp"] == 0 else ("ng",) if v["disp"] == 1 else ("rc",)
        self.tree.insert("", 0, values=(v["id"], cls, v["disp_name"], dest, f"{v['conf']:.2f}",
                                        f"{v['size_mm']:.2f}" if v["size_mm"] else "—", time.strftime("%H:%M:%S")),
                         tags=tag)
        for item in self.tree.get_children()[300:]:
            self.tree.delete(item)

    def on_ack(self, v):
        rtt = f"，往返 {v['rtt_ms']:.2f} ms" if "rtt_ms" in v else ""
        self.say(f"ACK {v.get('desc', hex(v['cmd_id']))}：{v['result_name']}{rtt}")

    def on_alarm(self, v):
        self.say(f"报警 code {v['code']} severity {v['severity']} detail {v['detail']}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--backend", default="virtual", choices=["virtual", "python-can", "controlcan", "zqwl"])
    ap.add_argument("--iface", default="socketcan")
    ap.add_argument("--channel", default="can0")
    ap.add_argument("--port", help="zqwl: COM port (default: found by USB VID 0x3562)")
    ap.add_argument("--dll", help="controlcan: vendor ControlCAN.dll (default: next to this file)")
    ap.add_argument("--dev-type", type=int, default=4, help="controlcan: VCI device type (ZLG USBCAN-2A = 4)")
    ap.add_argument("--dev-index", type=int, default=0)
    ap.add_argument("--can-index", type=int, default=0, help="zqwl / controlcan: channel on the adapter (0 = first)")
    a = ap.parse_args()
    bus = open_bus(a.backend, a.iface, a.channel, a.dll, a.dev_type, a.dev_index, a.can_index, a.port)
    root = tk.Tk()
    link = Link(bus)
    App(root, link, load_names())
    root.mainloop()
    link.quit = True
    bus.close()


if __name__ == "__main__":
    main()
