/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_CONNMARK_H
#define _XT_CONNMARK_H

#include <linux/types.h>

/* Operation modes for CONNMARK target */
enum {
    XT_CONNMARK_SET = 0,
    XT_CONNMARK_SAVE = 1,
    XT_CONNMARK_RESTORE = 2,
};

/* Bit shift direction */
enum {
    D_SHIFT_LEFT = 0,
    D_SHIFT_RIGHT = 1,
};

/* v1 target info */
struct xt_connmark_tginfo1 {
    __u32 ctmark;
    __u32 ctmask;
    __u32 nfmask;
    __u8  mode;
};

/* v2 target info (adds shift params) */
struct xt_connmark_tginfo2 {
    __u32 ctmark;
    __u32 ctmask;
    __u32 nfmask;
    __u8  mode;
    __u8  shift_dir;
    __u8  shift_bits;
};

/* match info */
struct xt_connmark_mtinfo1 {
    __u32 mark;
    __u32 mask;
    __u8  invert;
};

#endif /* _XT_CONNMARK_H */

/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _XT_CONNMARK_H
#define _XT_CONNMARK_H

#include <linux/netfilter/xt_connmark.h>

#endif /* _XT_CONNMARK_H */
