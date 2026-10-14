/* test_can: CAN protocol v1 (hw/can_proto.h) and SocketCAN link test on the board.
 * Run after `board/can_up.sh can0 loopback` (self test) or with a real partner on the bus.
 *   ./test_can [ifname] [roundtrips]     exit code = failed checks
 * Two sockets on the same interface play the PLC and DragonEyeSorter: frames one sends, the other receives.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "can_link.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...)                                              \
    do {                                                              \
        if (cond) g_pass++;                                           \
        else { g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__);  \
               printf(__VA_ARGS__); printf("\n"); }                   \
    } while (0)

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

/* receive the next frame with the wanted id (other ids are skipped), 0 on timeout */
static int recv_id(int fd, uint32_t id, canp_frame_t *f, int timeout_ms)
{
    double end = now_us() + timeout_ms * 1000.0;
    for (;;) {
        int left = (int)((end - now_us()) / 1000);
        if (left < 0) return 0;
        int r = can_recv(fd, f, left);
        if (r <= 0) return 0;
        if (f->id == id) return 1;
    }
}

static void test_codec(void)
{
    canp_status_t s = {CANP_ST_RUN, CANP_FLAG_BELT_MOVING | CANP_FLAG_PLC_LINK, 7, 42, 123.4, 3600};
    canp_frame_t f = canp_status(&s);
    canp_status_t d = canp_status_decode(&f);
    CHECK(f.id == CANP_ID_STATUS && f.len == 8, "status frame header");
    CHECK(d.state == s.state && d.flags == s.flags && d.alarm == 7 && d.hb == 42 && d.uptime_s == 3600 &&
              d.speed_mm_s > 123.39 && d.speed_mm_s < 123.41, "status round trip");
    canp_counts_t c = {60000, 41000, 15000, 4000};
    f = canp_counts(&c);
    canp_counts_t cd = canp_counts_decode(&f);
    CHECK(cd.total == 60000 && cd.ok == 41000 && cd.ng == 15000 && cd.recheck == 4000, "counts round trip");
    canp_part_t p = {513, 4, CANP_DISP_PICK, 1, 93, 49.87};
    f = canp_part(&p);
    canp_part_t pd = canp_part_decode(&f);
    CHECK(pd.id == 513 && pd.cls == 4 && pd.disp == CANP_DISP_PICK && pd.dest == 1 && pd.conf_pct == 93 &&
              pd.size_mm > 49.86 && pd.size_mm < 49.88, "part round trip");
    f = canp_speed(87.65, 9);
    CHECK(f.id == CANP_ID_SPEED && canp_get16(f.data) == 877 && f.data[2] == 9, "speed encoding (0.1 mm/s, LE)");
    f = canp_ack(CANP_ID_SPEED, 9, CANP_ACK_OK);
    CHECK(f.data[0] == 0x01 && f.data[1] == 9 && f.data[2] == 0, "ack carries the command id low byte and seq");
}

int main(int argc, char **argv)
{
    const char *ifn = argc > 1 ? argv[1] : "can0";
    int n = argc > 2 ? atoi(argv[2]) : 1000;
    test_codec();

    int plc = can_open(ifn), des = can_open(ifn);
    if (plc < 0 || des < 0) {
        perror("can_open (interface up? see board/can_up.sh)");
        return 1;
    }
    long tx_err0 = can_stat(ifn, "tx_errors"), rx_err0 = can_stat(ifn, "rx_errors");
    canp_frame_t f;
    while (can_recv(plc, &f, 0) > 0 || can_recv(des, &f, 0) > 0) {}   /* drain */

    /* 1) command -> ack round trips */
    double rtt_min = 1e18, rtt_max = 0, rtt_sum = 0;
    int ok = 0, bad = 0;
    for (int i = 0; i < n; i++) {
        uint8_t seq = (uint8_t)i, cmd = (uint8_t)(1 + i % 5);
        canp_frame_t c = canp_cmd(cmd, seq);
        double t0 = now_us();
        if (can_send(plc, &c) != 0) { bad++; continue; }
        canp_frame_t got;
        if (!recv_id(des, CANP_ID_CMD, &got, 100) || got.len != 2 || got.data[0] != cmd || got.data[1] != seq) {
            bad++;
            continue;
        }
        canp_frame_t a = canp_ack(got.id, got.data[1], CANP_ACK_OK);
        can_send(des, &a);
        if (!recv_id(plc, CANP_ID_ACK, &got, 100) || got.data[0] != (uint8_t)CANP_ID_CMD || got.data[1] != seq) {
            bad++;
            continue;
        }
        double rtt = now_us() - t0;
        rtt_sum += rtt;
        if (rtt < rtt_min) rtt_min = rtt;
        if (rtt > rtt_max) rtt_max = rtt;
        ok++;
    }
    printf("command -> ack: %d / %d ok, round trip min %.0f us, avg %.0f us, max %.0f us\n", ok, n, rtt_min,
           ok ? rtt_sum / ok : 0, rtt_max);
    CHECK(ok == n && bad == 0, "%d round trips failed", bad);

    /* 2) streams at fixed rates: receiver checks every frame arrives, in order, intact.
     * The application sends <= ~100 frames/s; on the board in loopback mode the controller silently loses frames
     * when they are sent back to back (TX counter complete, RX counter short, no error counter moves). */
    const int rates[] = {200, 1000, 2000, 0};   /* frames/s, 0 = as fast as possible */
    for (int ri = 0; ri < 4; ri++) {
        const int rate = rates[ri], total = rate ? (rate < 1000 ? rate : 2000) : 2000;
        int received = 0, wrong = 0;
        double t0 = now_us(), next = t0;
        for (int i = 0; i < total; i++) {
            if (rate) {
                next += 1e6 / rate;
                while (now_us() < next)
                    while (can_recv(des, &f, 0) > 0)
                        if (f.id == CANP_ID_SPEED) {
                            if (f.data[2] != (uint8_t)received) wrong++;
                            received++;
                        }
            }
            canp_frame_t sp = canp_speed(i * 0.1, (uint8_t)i);
            if (can_send(plc, &sp) != 0) break;
            while (can_recv(des, &f, 0) > 0)
                if (f.id == CANP_ID_SPEED) {
                    if (f.data[2] != (uint8_t)received) wrong++;
                    received++;
                }
        }
        while (received < total && can_recv(des, &f, 200) > 0)
            if (f.id == CANP_ID_SPEED) {
                if (f.data[2] != (uint8_t)received) wrong++;
                received++;
            }
        double el = (now_us() - t0) / 1e6;
        printf("stream %5s frames/s: %d / %d received, %d out of order, actual %.0f frames/s\n",
               rate ? (rate == 200 ? "200" : rate == 1000 ? "1000" : "2000") : "max", received, total, wrong,
               total / el);
        if (rate && rate <= 1000) CHECK(received == total && wrong == 0, "%d frames/s: received %d / %d", rate, received, total);
        while (can_recv(des, &f, 50) > 0) {}   /* drain before the next rate */
    }

    /* 3) status stream DES -> PLC */
    int st_ok = 0;
    for (int i = 0; i < 100; i++) {
        canp_status_t s = {CANP_ST_RUN, CANP_FLAG_PLC_LINK, 0, (uint8_t)i, 50.0, (uint16_t)i};
        canp_frame_t sf = canp_status(&s);
        can_send(des, &sf);
        if (recv_id(plc, CANP_ID_STATUS, &f, 100)) {
            canp_status_t d = canp_status_decode(&f);
            if (d.hb == (uint8_t)i && d.uptime_s == i) st_ok++;
        }
    }
    CHECK(st_ok == 100, "status stream: %d / 100", st_ok);

    long tx_err = can_stat(ifn, "tx_errors") - tx_err0, rx_err = can_stat(ifn, "rx_errors") - rx_err0;
    printf("interface error counters during the test: tx %ld, rx %ld\n", tx_err, rx_err);
    CHECK(tx_err == 0 && rx_err == 0, "bus errors tx %ld rx %ld", tx_err, rx_err);
    can_close(plc);
    can_close(des);
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail;
}
