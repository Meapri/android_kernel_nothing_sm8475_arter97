#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <unistd.h>
#include <linux/netlink.h>
#include <linux/input.h>

// Goodix driver netlink family from drivers/input/fingerprint/netlink.c
#define NETLINK_GOODIX 25

static char HBM_NODE[128] = ""; // prefer finger_hbm, fallback to hbm_mode
static char FOD_NODE[160] = ""; // autodetect goodix_ts.X

static volatile sig_atomic_t g_stop = 0;
static void handle_sig(int sig) { (void)sig; g_stop = 1; }

static void log_print(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    char buf[512]; vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    fprintf(stderr, "[udfpsd] %s\n", buf);
}

static int write_int(const char *path, int v) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -errno;
    char s[8]; int n = snprintf(s, sizeof(s), "%d", v);
    ssize_t w = write(fd, s, n);
    int err = (w == n) ? 0 : -errno;
    close(fd);
    return err;
}

static void detect_nodes(void) {
    // HBM: finger_hbm first (UDFPS-specific), else hbm_mode
    if (access("/sys/panel_feature/finger_hbm", W_OK) == 0) {
        strncpy(HBM_NODE, "/sys/panel_feature/finger_hbm", sizeof(HBM_NODE)-1);
    } else if (access("/sys/panel_feature/hbm_mode", W_OK) == 0) {
        strncpy(HBM_NODE, "/sys/panel_feature/hbm_mode", sizeof(HBM_NODE)-1);
    } else {
        HBM_NODE[0] = '\0';
    }

    // FOD enable path: try goodix_ts.0/1 and un-suffixed
    const char *candidates[] = {
        "/sys/devices/platform/goodix_ts.0/gesture/fod_en",
        "/sys/devices/platform/goodix_ts.1/gesture/fod_en",
        "/sys/devices/platform/goodix_ts/gesture/fod_en",
    };
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); ++i) {
        if (access(candidates[i], W_OK) == 0) {
            strncpy(FOD_NODE, candidates[i], sizeof(FOD_NODE)-1);
            break;
        }
    }
}

static int nl_send_register(int sockfd, uint32_t pid) {
    char payload = 'R';
    struct {
        struct nlmsghdr nlh;
        char data;
    } msg;
    memset(&msg, 0, sizeof(msg));
    msg.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(char));
    msg.nlh.nlmsg_pid = pid;
    msg.data = payload;
    struct sockaddr_nl nladdr = { .nl_family = AF_NETLINK };
    struct iovec iov = { .iov_base = &msg, .iov_len = msg.nlh.nlmsg_len };
    struct msghdr msgh = { .msg_name = &nladdr, .msg_namelen = sizeof(nladdr), .msg_iov = &iov, .msg_iovlen = 1 };
    return sendmsg(sockfd, &msgh, 0);
}

static int setup_goodix_nl(void) {
    int s = socket(AF_NETLINK, SOCK_RAW, NETLINK_GOODIX);
    if (s < 0) return -errno;
    struct sockaddr_nl addr; memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK; addr.nl_pid = getpid();
    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        int e = -errno; close(s); return e;
    }
    // Register pid so kernel can unicast back
    nl_send_register(s, addr.nl_pid);
    return s;
}

static int setup_uevent_nl(void) {
    int s = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (s < 0) return -errno;
    struct sockaddr_nl addr; memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK; addr.nl_pid = getpid(); addr.nl_groups = 0xFFFFFFFF; // all groups
    if (bind(s, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        int e = -errno; close(s); return e;
    }
    return s;
}

static void handle_down(void) {
    if (FOD_NODE[0]) write_int(FOD_NODE, 1);
    if (HBM_NODE[0]) write_int(HBM_NODE, 1);
    log_print("DOWN -> FOD=1, HBM=1 (HBM:%s)", HBM_NODE[0] ? HBM_NODE : "none");
}

static void handle_up(void) {
    if (HBM_NODE[0]) write_int(HBM_NODE, 0);
    log_print("UP -> HBM=0 (HBM:%s)", HBM_NODE[0] ? HBM_NODE : "none");
}

/* ---------- evdev fallback: listen for KEY_FINGER from touchscreen ---------- */
static int open_evdev_nodes(int epfd) {
    int opened = 0;
    for (int i = 0; i < 64; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", i);
        int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) continue;

        unsigned long evbit[(EV_MAX + 64) / 64];
        memset(evbit, 0, sizeof(evbit));
        if (ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit) < 0) {
            close(fd); continue;
        }
        bool supports_keys = (evbit[EV_KEY / 64] & (1ULL << (EV_KEY % 64))) != 0;
        if (!supports_keys) { close(fd); continue; }

        // Check KEY_FINGER support explicitly
        unsigned long keybit[(KEY_MAX + 64) / 64];
        memset(keybit, 0, sizeof(keybit));
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) < 0) {
            close(fd); continue;
        }
        bool has_key_finger = (keybit[KEY_FINGER / 64] & (1ULL << (KEY_FINGER % 64))) != 0;
        if (!has_key_finger) { close(fd); continue; }

        struct epoll_event ev; memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN; ev.data.fd = fd; // identify by fd
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) == 0) {
            log_print("evdev attached: %s", path);
            opened++;
        } else {
            close(fd);
        }
    }
    return opened;
}

int main(void) {
    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);
    detect_nodes();
    if (!HBM_NODE[0]) {
        log_print("No writable HBM sysfs node (finger_hbm/hbm_mode) found");
    }
    if (!FOD_NODE[0]) {
        log_print("No writable FOD enable node found");
    }

    int s_goodix = setup_goodix_nl();
    int s_uevent = setup_uevent_nl();

    int ep = epoll_create1(EPOLL_CLOEXEC);
    if (ep < 0) {
        log_print("epoll_create failed: %d", errno);
        return 0;
    }
    struct epoll_event ev; memset(&ev, 0, sizeof(ev));
    if (s_goodix >= 0) { ev.events = EPOLLIN; ev.data.u32 = 1; epoll_ctl(ep, EPOLL_CTL_ADD, s_goodix, &ev); }
    if (s_uevent >= 0) { ev.events = EPOLLIN; ev.data.u32 = 2; epoll_ctl(ep, EPOLL_CTL_ADD, s_uevent, &ev); }
    int evdev_count = open_evdev_nodes(ep);
    if (s_goodix < 0 && s_uevent < 0 && evdev_count == 0) {
        log_print("no event sources available (goodix/netlink, uevent, evdev)");
        return 0;
    }

    char buf[2048];
    while (!g_stop) {
        struct epoll_event out;
        int n = epoll_wait(ep, &out, 1, -1);
        if (n <= 0) continue;
        if (out.data.u32 == 1) {
            // Goodix netlink
            ssize_t r = recv(s_goodix, buf, sizeof(buf), 0);
            if (r <= 0) continue;
            struct nlmsghdr *nlh = (struct nlmsghdr*)buf;
            if (NLMSG_OK(nlh, r)) {
                char code = *((char*)NLMSG_DATA(nlh));
                if (code == '1' || code == 'D' || code == 1) handle_down();
                else if (code == '0' || code == 'U' || code == 0) handle_up();
            }
        } else if (out.data.u32 == 2) {
            // uevent: handle panel blank/unblank as safety
            ssize_t r = recv(s_uevent, buf, sizeof(buf)-1, 0);
            if (r <= 0) continue; buf[r] = '\0';
            if (strstr(buf, "change@/devices/virtual/graphics/fb") && strstr(buf, "BLANK")) {
                handle_up();
            }
        } else if (out.data.u32 >= 100 && out.data.u32 < 200) {
            // evdev input
            struct input_event iev;
            ssize_t r = read(out.data.fd, &iev, sizeof(iev));
            if (r == sizeof(iev)) {
                if (iev.type == EV_KEY && iev.code == KEY_FINGER) {
                    if (iev.value) handle_down(); else handle_up();
                }
            }
        }
    }

    if (s_goodix >= 0) close(s_goodix);
    if (s_uevent >= 0) close(s_uevent);
    return 0;
}



