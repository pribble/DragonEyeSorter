/* canplc: plays the line PLC on the CAN bus (protocol v1, hw/can_proto.h). Reference for the PC-side program and a
 * test partner on the board (loopback: scripts/deploy/out/can_up.sh can0 loopback).
 *   canplc <if> start|stop|pause|resume|reset        send a command, wait for the ACK
 *   canplc <if> speed <mm/s> | recipe <n>
 *   canplc <if> monitor [sec]                        print STATUS / COUNTS / PART / ALARM, send PLC heartbeats (5 Hz)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "can_link.h"
static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static const char *state_name(int s)
{
    static const char *const n[] = {"idle", "run", "paused", "estop", "fault"};
    return (unsigned)s < sizeof n / sizeof n[0] ? n[s] : "?";
}
/* flags in STATUS, in the order they are printed */
static const struct { uint8_t bit; const char *name; } k_flags[] = {
    {CANP_FLAG_BELT_MOVING, "belt-moving"}, {CANP_FLAG_ARM_BUSY, "arm-busy"}, {CANP_FLAG_ALARM, "ALARM"},
    {CANP_FLAG_PLC_LINK, "plc-link"},       {CANP_FLAG_CALIBRATED, "calibrated"},
};
static void print_frame(const canp_frame_t *f)
{
    if (f->id == CANP_ID_STATUS) {
        const canp_status_t s = canp_status_decode(f);
        printf("STATUS  %-6s flags", state_name(s.state));
        for (size_t i = 0; i < sizeof k_flags / sizeof k_flags[0]; i++)
            if (s.flags & k_flags[i].bit) printf(" %s", k_flags[i].name);
        printf("  speed %.1f mm/s  alarm %u  hb %u  up %us\n", s.speed_x10 / 10.0, s.alarm, s.hb, s.uptime_s);
    } else if (f->id == CANP_ID_COUNTS) {
        const canp_counts_t c = canp_counts_decode(f);
        printf("COUNTS  total %u  ok %u  ng %u  recheck %u\n", c.total, c.ok, c.ng, c.recheck);
    } else if (f->id == CANP_ID_PART) {
        const canp_part_t p = canp_part_decode(f);
        static const char *const d[] = {"pick", "flap", "recheck"};
        printf("PART    #%u  class %d  %s -> %u  conf %u%%  size %.2f mm\n", p.id, p.cls == 0xFF ? -1 : p.cls,
               p.disp <= 2 ? d[p.disp] : "?", p.dest, p.conf, p.size_x100 / 100.0);
    } else if (f->id == CANP_ID_ALARM) {
        printf("ALARM   code %u  severity %u  detail %u\n", f->data[0], f->data[1], canp_get16(f->data + 2));
    }
}
int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: canplc <if> start|stop|pause|resume|reset|speed <mm/s>|recipe <n>|monitor [sec]\n");
        return 1;
    }
    int fd = can_open(argv[1]);
    if (fd < 0) { perror("can_open"); return 1; }
    const char *cmd = argv[2];
    uint8_t seq = (uint8_t)(time(NULL) & 0xff);
    static const struct { const char *name; uint8_t cmd; } k_cmds[] = {
        {"start", CANP_CMD_START}, {"stop", CANP_CMD_STOP}, {"pause", CANP_CMD_PAUSE},
        {"resume", CANP_CMD_RESUME}, {"reset", CANP_CMD_RESET_ALARM},
    };
    canp_frame_t out, in;
    int have_cmd = 0;
    for (size_t i = 0; i < sizeof k_cmds / sizeof k_cmds[0] && !have_cmd; i++)
        if (!strcmp(cmd, k_cmds[i].name)) out = canp_cmd(k_cmds[i].cmd, seq), have_cmd = 1;
    if (!have_cmd && !strcmp(cmd, "speed") && argc > 3) out = canp_speed(atof(argv[3]), seq), have_cmd = 1;
    else if (!have_cmd && !strcmp(cmd, "recipe") && argc > 3) out = canp_recipe((uint8_t)atoi(argv[3]), seq), have_cmd = 1;
    if (have_cmd) {
        if (can_send(fd, &out) != 0) { perror("send"); return 1; }
        double t0 = now_s();
        while (now_s() - t0 < 0.5) {
            if (can_recv(fd, &in, 50) == 1 && in.id == CANP_ID_ACK && in.data[0] == (uint8_t)out.id && in.data[1] == seq) {
                static const char *const r[] = {"ok", "rejected (state)", "bad value", "unknown"};
                printf("%s: ACK %s after %.1f ms\n", cmd, in.data[2] <= 3 ? r[in.data[2]] : "?", (now_s() - t0) * 1e3);
                can_close(fd);
                return in.data[2] == CANP_ACK_OK ? 0 : 2;
            }
        }
        printf("%s: no ACK within 500 ms\n", cmd);
        can_close(fd);
        return 3;
    }
    if (!strcmp(cmd, "monitor")) {
        double secs = argc > 3 ? atof(argv[3]) : 5, t0 = now_s(), next_hb = t0;
        uint8_t hb = 0;
        while (now_s() - t0 < secs) {
            if (now_s() >= next_hb) {
                canp_frame_t h = canp_plc_hb(hb++);
                can_send(fd, &h);
                next_hb += 0.2;
            }
            if (can_recv(fd, &in, 20) == 1) print_frame(&in);
        }
        can_close(fd);
        return 0;
    }
    fprintf(stderr, "unknown command %s\n", cmd);
    return 1;
}
