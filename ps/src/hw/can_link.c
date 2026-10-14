/* can_link.c — see can_link.h */
#include "can_link.h"

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

int can_open(const char *ifname)
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

void can_close(int fd)
{
    if (fd >= 0) close(fd);
}

int can_send(int fd, const canp_frame_t *f)
{
    struct can_frame cf;
    memset(&cf, 0, sizeof cf);
    cf.can_id = f->id & CAN_SFF_MASK;
    cf.can_dlc = f->len > 8 ? 8 : f->len;
    memcpy(cf.data, f->data, cf.can_dlc);
    for (int tries = 0; tries < 50; tries++) {   /* TX queue full (ENOBUFS / EAGAIN): wait for it to drain */
        if (write(fd, &cf, sizeof cf) == (ssize_t)sizeof cf) return 0;
        if (errno != ENOBUFS && errno != EAGAIN) return -1;
        struct pollfd p = {fd, POLLOUT, 0};
        poll(&p, 1, 2);
    }
    return -1;
}

int can_try_send(int fd, const canp_frame_t *f)
{
    struct can_frame cf;
    memset(&cf, 0, sizeof cf);
    cf.can_id = f->id & CAN_SFF_MASK;
    cf.can_dlc = f->len > 8 ? 8 : f->len;
    memcpy(cf.data, f->data, cf.can_dlc);
    if (write(fd, &cf, sizeof cf) == (ssize_t)sizeof cf) return 0;
    return (errno == ENOBUFS || errno == EAGAIN) ? 1 : -1;
}

int can_recv(int fd, canp_frame_t *f, int timeout_ms)
{
    struct pollfd p = {fd, POLLIN, 0};
    int r = poll(&p, 1, timeout_ms);
    if (r <= 0) return r < 0 ? -1 : 0;
    struct can_frame cf;
    ssize_t n = read(fd, &cf, sizeof cf);
    if (n != (ssize_t)sizeof cf) return -1;
    if (cf.can_id & (CAN_ERR_FLAG | CAN_RTR_FLAG | CAN_EFF_FLAG)) return 0;   /* only plain 11-bit data frames */
    f->id = cf.can_id & CAN_SFF_MASK;
    f->len = cf.can_dlc;
    memcpy(f->data, cf.data, 8);
    return 1;
}

long can_stat(const char *ifname, const char *name)
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
