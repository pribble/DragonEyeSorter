/* test_servo_gap.c — how long a servo needs after its reply before it accepts the next frame.
 *
 * 2026-10-17, first servos on the bus (PL v9, BSS138, 4.9 V supply): the 15 kg body servos (ID 1..5) ignored every
 * other back-to-back PING / READ (strict ok / timeout alternation, no RX errors), the gripper (ID 6) answered all;
 * with a few ms between calls all answered. This measures the threshold: PINGs with a controlled idle time from the
 * end of the previous reply (PL timestamp) to the start of the next frame.
 *
 *   test_servo_gap <id> [n per gap]      read-only (PING); stop des_app first (single-master bus)
 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "des_hw.h"

static void spin_until(des_hw_t *h, uint32_t t_start, double us)
{
    while (des_ticks_us(des_now(h) - t_start) < us) {
        if (us - des_ticks_us(des_now(h) - t_start) > 300) {
            struct timespec ts = {0, 100000};
            nanosleep(&ts, NULL);
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_servo_gap <id> [n per gap]\n");
        return 2;
    }
    const uint8_t id = (uint8_t)strtoul(argv[1], NULL, 0);
    const int n = argc > 2 ? atoi(argv[2]) : 40;
    des_hw_t hw;
    if (des_hw_open(&hw) != DES_OK) {
        fprintf(stderr, "des_hw_open failed (PL bitstream with des_regs?)\n");
        return 1;
    }
    des_servo_set_quiet_us(0);   /* measure the servo itself, without des_hw's own quiet time */
    static const double all_gaps[] = {200, 500, 1000, 2000, 5000, 10000, 15000, 20000, 30000, 50000, 100000};
    const double *gaps_us = all_gaps;
    const unsigned n_gaps = sizeof all_gaps / sizeof all_gaps[0];
    printf("servo %u, %d PINGs per gap; gap = end of previous reply -> start of next frame (PL timestamps)\n", id, n);
    for (unsigned g = 0; g < n_gaps; g++) {
        des_sb_result_t r;
        (void)des_servo_ping(&hw, id, &r);   // prime: start from a known state (and after a long idle)
        struct timespec ts = {0, 50 * 1000000};
        nanosleep(&ts, NULL);
        int rc = des_servo_ping(&hw, id, &r);
        uint32_t t_prev = r.t_rx;
        int prev_ok = rc == DES_OK;
        int ok = 0, to = 0, other = 0;
        double gmin = 1e9, gmax = 0, lat_sum = 0;
        char pat[64] = {0};
        for (int i = 0; i < n; i++) {
            if (prev_ok) spin_until(&hw, t_prev, gaps_us[g]);   // after a timeout: send at once (it is idle anyway)
            rc = des_servo_ping(&hw, id, &r);
            const double gap = des_ticks_us(r.t_tx0 - t_prev);
            if (prev_ok) {
                if (gap < gmin) gmin = gap;
                if (gap > gmax) gmax = gap;
            }
            if (rc == DES_OK) {
                ok++;
                lat_sum += des_ticks_us(r.t_rx - r.t_tx1);
                t_prev = r.t_rx;
            } else {
                if (rc == DES_E_TIMEOUT) to++; else other++;
                t_prev = des_now(&hw);
            }
            if (i < (int)sizeof pat - 1) pat[i] = rc == DES_OK ? 'o' : 'x';
            prev_ok = rc == DES_OK;
        }
        printf("gap %6.0f us (actual %6.0f..%6.0f): ok %2d  timeout %2d  other %d  reply %.0f us  %s\n", gaps_us[g],
               gmin < 1e9 ? gmin : 0, gmax, ok, to, other, ok ? lat_sum / ok : 0, pat);
    }
    des_hw_close(&hw);
    return 0;
}
