/* modbus_map.h — DragonEyeSorter Modbus-TCP register map v1 (docs/软件与UI.md「Modbus-TCP」).
 * The board is the slave (server, port 502); the PLC / SCADA is the client. Same codes and rules as the CAN protocol
 * (can_proto.h): commands CANP_CMD_*, results CANP_ACK_*, states CANP_ST_*, flags CANP_FLAG_*, dispositions
 * CANP_DISP_*. Addresses are 0-based (PLC software that counts from 1 shows 40001.. / 30001..).
 * 32-bit values: high word first. Unit id ignored.
 */
#ifndef DES_MODBUS_MAP_H
#define DES_MODBUS_MAP_H

#define MB_MAP_VERSION 0x0100

/* holding registers (FC 03 read, FC 06 / 16 write). A write is executed in the next control cycle (<= 17 ms);
 * then SEQ goes + 1 and RESULT holds its CANP_ACK_* code. */
enum {
    MB_HR_CMD = 0,       /* write CANP_CMD_* (1 start, 2 stop, 3 pause, 4 resume, 5 reset alarm); reads 0 */
    MB_HR_SPEED = 1,     /* belt speed set point, 0.1 mm/s (1..2000); reads the current set point */
    MB_HR_RECIPE = 2,    /* recipe id (only while not running); reads the active recipe */
    MB_HR_RESULT = 3,    /* CANP_ACK_* of the last handled write (read only) */
    MB_HR_SEQ = 4,       /* + 1 per handled write (read only): write, wait until it changes, read RESULT */
    MB_HR_TIME_HI = 5,   /* PLC wall clock, Unix seconds, high word ...                                     */
    MB_HR_TIME_LO = 6,   /* ... low word; write both in one FC 16 (the low word applies it). The board has no RTC */
    MB_N_HR = 8
};

/* input registers (FC 04), refreshed every control cycle */
enum {
    MB_IR_STATE = 0,         /* CANP_ST_* */
    MB_IR_FLAGS = 1,         /* CANP_FLAG_* (PLC_LINK: a CAN heartbeat or a Modbus request within 1 s) */
    MB_IR_TOTAL = 2,         /* 2..9: counters total / ok / ng / recheck, 32 bit each (hi, lo); the values are the
                                CAN counters, which wrap at 65535 */
    MB_IR_OK = 4,
    MB_IR_NG = 6,
    MB_IR_RECHECK = 8,
    MB_IR_PART_SEQ = 10,     /* + 1 per decided part (wraps); a gap = parts the PLC did not see (counters stay exact) */
    MB_IR_PART_ID = 11,      /* last decided part: track id */
    MB_IR_PART_CLS = 12,     /* class index (label_list), 0xFFFF = none */
    MB_IR_PART_DISP = 13,    /* CANP_DISP_* */
    MB_IR_PART_DEST = 14,    /* bin (pick) / outlet (gate) index */
    MB_IR_PART_SIZE = 15,    /* gear OD, 0.01 mm (0 = not measured) */
    MB_IR_PART_CONF = 16,    /* class confidence, % */
    MB_IR_SPEED = 17,        /* actual belt speed, 0.1 mm/s */
    MB_IR_HB = 18,           /* + 1 per control cycle */
    MB_IR_UPTIME = 19,       /* s since start (wraps) */
    MB_IR_ALARM = 20,        /* alarm code (1 = e-stop), 0 = none */
    MB_IR_RECIPE = 21,       /* active recipe id */
    MB_IR_VERSION = 22,      /* MB_MAP_VERSION */
    MB_N_IR = 24
};

#endif
