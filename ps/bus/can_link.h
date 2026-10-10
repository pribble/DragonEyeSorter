/* can_link.h — minimal SocketCAN wrapper (raw classic frames). The interface must be up:
 *   ip link set can0 type can bitrate 500000 && ip link set can0 up        (bus with a partner)
 *   ip link set can0 type can bitrate 500000 dbitrate 2000000 fd on loopback on && ip link set can0 up
 *                                                                          (self test: ipms_canfd loops back only in FD mode)
 */
#ifndef CAN_LINK_H
#define CAN_LINK_H

#include "can_proto.h"

#ifdef __cplusplus
extern "C" {
#endif

int can_open(const char *ifname);                              /* fd, or -1 (errno set) */
void can_close(int fd);
int can_send(int fd, const canp_frame_t *f);                   /* 0 ok, -1 error */
int can_recv(int fd, canp_frame_t *f, int timeout_ms);         /* 1 frame, 0 timeout, -1 error */
/* Interface counters from /sys/class/net/<if>/statistics (tx/rx packets and errors); -1 if unreadable. */
long can_stat(const char *ifname, const char *name);

#ifdef __cplusplus
}
#endif

#endif
