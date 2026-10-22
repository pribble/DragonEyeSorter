/* can_link.h — minimal SocketCAN wrapper (raw classic frames). The interface must be up:
 *   ip link set can0 type can bitrate 500000 && ip link set can0 up
 *   ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on loopback on && ip link set can0 up
 *                                                                          (self test: ipms_canfd loops back only in FD mode)
 * Header-only (static inline): a handful of calls, no library object to link. The interface keeps the classic-CAN
 * contract of protocol v1 (can_proto.h); multi-byte payload semantics are the caller's business.
 */
#ifndef CAN_LINK_H
#define CAN_LINK_H

#include <errno.h>
#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "can_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

/* one marshalling site for send / try_send: plain 11-bit data frames only */
static inline void can_marshal(const canp_frame_t *f, struct can_frame *cf)
{
    memset(cf, 0, sizeof *cf);
    cf->can_id = f->id & CAN_SFF_MASK;
    cf->can_dlc = f->len > 8 ? 8 : f->len;
    memcpy(cf->data, f->data, cf->can_dlc);
}
static inline int can_open(const char *ifname)   /* fd, or -1 (errno set) */
{
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) return -1;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) { close(fd); return -1; }
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof addr);
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    return fd;
}
static inline void can_close(int fd) { if (fd >= 0) close(fd); }
static inline int can_send(int fd, const canp_frame_t *f)   /* 0 ok, -1 error (waits up to 100 ms if the TX queue is full) */
{
    struct can_frame cf;
    can_marshal(f, &cf);
    for (int tries = 0; tries < 50; tries++) {   /* TX queue full (ENOBUFS / EAGAIN): wait for it to drain */
        if (write(fd, &cf, sizeof cf) == (ssize_t)sizeof cf) return 0;
        if (errno != ENOBUFS && errno != EAGAIN) return -1;
        struct pollfd p = {fd, POLLOUT, 0};
        poll(&p, 1, 2);
    }
    return -1;
}
static inline int can_try_send(int fd, const canp_frame_t *f)   /* one attempt: 0 ok, 1 TX queue full, -1 error */
{
    struct can_frame cf;
    can_marshal(f, &cf);
    if (write(fd, &cf, sizeof cf) == (ssize_t)sizeof cf) return 0;
    return (errno == ENOBUFS || errno == EAGAIN) ? 1 : -1;
}
static inline int can_recv(int fd, canp_frame_t *f, int timeout_ms)   /* 1 frame, 0 timeout, -1 error */
{
    struct pollfd p = {fd, POLLIN, 0};
    int r = poll(&p, 1, timeout_ms);
    if (r <= 0) return r < 0 ? -1 : 0;
    struct can_frame cf;
    ssize_t n = read(fd, &cf, sizeof cf);
    if (n != (ssize_t)sizeof cf) return -1;
    if (cf.can_id & (CAN_ERR_FLAG | CAN_RTR_FLAG | CAN_EFF_FLAG)) return 0;   /* only plain 11-bit data frames */
    f->id = (uint16_t)(cf.can_id & CAN_SFF_MASK);
    f->len = cf.can_dlc;
    memcpy(f->data, cf.data, 8);
    return 1;
}
/* Interface counters from /sys/class/net/<if>/statistics (tx/rx packets and errors); -1 if unreadable. */
static inline long can_stat(const char *ifname, const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "/sys/class/net/%s/statistics/%s", ifname, name);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    long v = -1;
    if (fscanf(f, "%ld", &v) != 1) v = -1;
    fclose(f);
    return v;
}
#ifdef __cplusplus
}
#endif
#endif
