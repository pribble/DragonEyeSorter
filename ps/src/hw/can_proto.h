/* can_proto.h — DragonEyeSorter <-> "PLC" CAN protocol v1 (docs/软件与UI.md "工业总线").
 * Classic CAN 2.0A (11-bit ids), 500 kbit/s (SN65HVD232 is classic only), multi-byte fields little-endian.
 *
 *   PLC -> DES                                    DES -> PLC
 *   0x100 CMD     [cmd, seq]                      0x180 ACK     [id_lo, seq, result]
 *   0x101 SPEED   [speed_x10 u16, seq]            0x181 STATUS  [state,flags,speed_x10 u16,alarm,hb,uptime_s u16] 10 Hz
 *   0x102 RECIPE  [recipe, seq]                   0x182 COUNTS  [total,ok,ng,recheck u16] 1 Hz + on change
 *   0x103 PLC_HB  [counter] (>= 2 Hz)             0x183 PART    [id u16,class,disp,dest,conf_pct,size_x100 u16]
 *   0x104 TIME    [unix_s u32, ms u16] (v1.1)     0x184 ALARM   [code,severity,detail u16]
 *   0x105 LAMP    [mode, lamps, seq] (v1.2)
 * LAMP (v1.2): the PLC takes over the tower light on the RS485 bus (mode 1 = lamps b0 green b1 yellow b2 red b3 buzzer,
 * mode 0 = back to the line state); executed as a Modbus-RTU write to the relay module (CAN -> RS485), ACK REJECTED when
 * the RS485 bus is not running. STATUS flag RS485 (v1.2) = the relay module answered within the last 2 s.
 * TIME has no ACK: it is the PLC wall clock; the board has no RTC and records wall = its clock + the offset from the
 * last TIME frame (the system clock is not changed). ACK echoes id_lo of the command id and the same seq; a missing
 * PLC heartbeat for 1 s clears a STATUS flag (the app may pause as an interlock).
 */
#ifndef CAN_PROTO_H
#define CAN_PROTO_H

#include <stdint.h>

enum {
    CANP_ID_CMD = 0x100, CANP_ID_SPEED = 0x101, CANP_ID_RECIPE = 0x102, CANP_ID_PLC_HB = 0x103, CANP_ID_TIME = 0x104,
    CANP_ID_LAMP = 0x105,
    CANP_ID_ACK = 0x180, CANP_ID_STATUS = 0x181, CANP_ID_COUNTS = 0x182, CANP_ID_PART = 0x183, CANP_ID_ALARM = 0x184,
};
enum { CANP_CMD_START = 1, CANP_CMD_STOP = 2, CANP_CMD_PAUSE = 3, CANP_CMD_RESUME = 4, CANP_CMD_RESET_ALARM = 5 };
enum { CANP_ACK_OK = 0, CANP_ACK_REJECTED = 1, CANP_ACK_BAD_VALUE = 2, CANP_ACK_UNKNOWN = 3 };
enum { CANP_ST_IDLE = 0, CANP_ST_RUN = 1, CANP_ST_PAUSED = 2, CANP_ST_ESTOP = 3, CANP_ST_FAULT = 4 };
enum {
    CANP_FLAG_BELT_MOVING = 0x01, CANP_FLAG_ARM_BUSY = 0x02, CANP_FLAG_ALARM = 0x04,
    CANP_FLAG_PLC_LINK = 0x08, CANP_FLAG_CALIBRATED = 0x10, CANP_FLAG_RS485 = 0x20,
};
enum { CANP_DISP_PICK = 0, CANP_DISP_FLAP = 1, CANP_DISP_RECHECK = 2 };

typedef struct { uint8_t len; uint8_t data[8]; uint16_t id; } canp_frame_t;   /* up to 8 payload bytes */
static inline void canp_put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline uint16_t canp_get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
/* physical -> wire units: the encoder math of protocol v1 (+0.5 truncation, saturation to the 16-bit field) */
static inline uint16_t canp_mm_s_x10(double mm_s)
{
    const double v = mm_s * 10 + 0.5;
    return (uint16_t)(v < 0 ? 0 : v > 65535 ? 65535 : v);
}
static inline uint16_t canp_mm_x100(double mm)
{
    const double v = mm * 100 + 0.5;
    return (uint16_t)(v < 0 ? 0 : v > 65535 ? 65535 : v);
}

/* ---- PLC -> DES ---- */
static inline canp_frame_t canp_cmd(uint8_t cmd, uint8_t seq) { canp_frame_t f = {2, {cmd, seq}, CANP_ID_CMD}; return f; }
static inline canp_frame_t canp_speed(double mm_s, uint8_t seq)
{
    canp_frame_t f = {3, {0}, CANP_ID_SPEED};
    canp_put16(f.data, canp_mm_s_x10(mm_s));
    f.data[2] = seq;
    return f;
}
static inline canp_frame_t canp_recipe(uint8_t r, uint8_t seq) { canp_frame_t f = {2, {r, seq}, CANP_ID_RECIPE}; return f; }
static inline canp_frame_t canp_plc_hb(uint8_t c) { canp_frame_t f = {1, {c}, CANP_ID_PLC_HB}; return f; }
static inline canp_frame_t canp_lamp(uint8_t mode, uint8_t lamps, uint8_t seq)
{
    canp_frame_t f = {3, {mode, lamps, seq}, CANP_ID_LAMP};
    return f;
}
static inline int64_t canp_time_ms(const canp_frame_t *f)   /* unix time in ms carried by a TIME frame */
{
    const uint32_t s = (uint32_t)canp_get16(f->data) | ((uint32_t)canp_get16(f->data + 2) << 16);
    return (int64_t)s * 1000 + canp_get16(f->data + 4);
}

/* ---- DES -> PLC ---- */
typedef struct { uint8_t state, flags, alarm, hb; uint16_t speed_x10; uint16_t uptime_s; } canp_status_t;
typedef struct { uint16_t total, ok, ng, recheck; } canp_counts_t;
typedef struct { uint16_t id; uint8_t cls, disp, dest, conf; uint16_t size_x100; } canp_part_t;

static inline canp_frame_t canp_ack(uint32_t cmd_id, uint8_t seq, uint8_t result)
{ canp_frame_t f = {3, {(uint8_t)cmd_id, seq, result}, CANP_ID_ACK}; return f; }
static inline canp_frame_t canp_status(const canp_status_t *s)
{
    canp_frame_t f = {8, {0}, CANP_ID_STATUS};
    f.data[0] = s->state; f.data[1] = s->flags;
    canp_put16(f.data + 2, s->speed_x10);
    f.data[4] = s->alarm; f.data[5] = s->hb;
    canp_put16(f.data + 6, s->uptime_s);
    return f;
}
static inline canp_status_t canp_status_decode(const canp_frame_t *f)
{
    canp_status_t s = {f->data[0], f->data[1], f->data[4], f->data[5], canp_get16(f->data + 2),
                       canp_get16(f->data + 6)};
    return s;
}
static inline canp_frame_t canp_counts(const canp_counts_t *c)
{
    canp_frame_t f = {8, {0}, CANP_ID_COUNTS};
    canp_put16(f.data, c->total); canp_put16(f.data + 2, c->ok);
    canp_put16(f.data + 4, c->ng); canp_put16(f.data + 6, c->recheck);
    return f;
}
static inline canp_counts_t canp_counts_decode(const canp_frame_t *f)
{
    canp_counts_t c = {canp_get16(f->data), canp_get16(f->data + 2), canp_get16(f->data + 4), canp_get16(f->data + 6)};
    return c;
}
static inline canp_frame_t canp_part(const canp_part_t *p)
{
    canp_frame_t f = {8, {0}, CANP_ID_PART};
    canp_put16(f.data, p->id);
    f.data[2] = p->cls; f.data[3] = p->disp; f.data[4] = p->dest; f.data[5] = p->conf;
    canp_put16(f.data + 6, p->size_x100);
    return f;
}
static inline canp_part_t canp_part_decode(const canp_frame_t *f)
{
    canp_part_t p = {canp_get16(f->data), f->data[2], f->data[3], f->data[4], f->data[5], canp_get16(f->data + 6)};
    return p;
}
static inline canp_frame_t canp_alarm(uint8_t code, uint8_t severity, uint16_t detail)
{
    canp_frame_t f = {4, {code, severity, 0, 0}, CANP_ID_ALARM};
    canp_put16(f.data + 2, detail);
    return f;
}

#endif
