/*
 * ps5vr-helper - PS5VR's resident helper payload.
 *
 * An app is jailed: /data and USB drives do not resolve inside it. PS5VR
 * plays videos from those places, so this helper gives the PS5VR process
 * (title PPSA99177) the real root directory, the way etaHEN's jailbreak
 * daemon does for homebrew media players.
 *
 * Only when PS5VR asks: it connects to 127.0.0.1:9079 and sends
 * "unjail <pid>" once it has loaded every system module it needs, and
 * "elevate <pid> <level>" when it starts hand tracking (see elevate()). Library
 * loads go through the sandbox's /<random word>/common/lib path, which does
 * not exist under the real root, so a process unjailed while it is still
 * starting fails to start (PRX_PROCESS_STARTUP_FAILURE).
 *
 * Load it once after boot (nc <ps5> 9021 < ps5vr-helper.elf, or the
 * autoloader). A second copy replaces the first.
 */
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <ps5/kernel.h>
#include <ps5/klog.h>

#define TITLE "PPSA99177"
#define NAME  "ps5vr-helper"
#define PORT  9079

typedef struct app_info {
    uint32_t app_id;
    uint64_t unknown1;
    char     title_id[14];
    char     unknown2[0x3c];
} app_info_t;
int sceKernelGetAppInfo(pid_t pid, app_info_t *info);
int sceKernelSendNotificationRequest(int, void *, unsigned long, int);

static void say(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    printf("[" NAME "] %s\n", line);
    klog_printf("[" NAME "] %s\n", line);
}

/* Walk the process list: callback(pid, comm) for each. */
static void for_each_proc(void (*fn)(pid_t, const char *, void *), void *arg)
{
    int mib[4] = {1, 14, 8, 0};     /* kern.proc.proc */
    size_t size = 0;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) != 0)
        return;
    uint8_t *buf = malloc(size);
    if (!buf)
        return;
    if (sysctl(mib, 4, buf, &size, NULL, 0) == 0) {
        for (uint8_t *p = buf; p < buf + size;) {
            int structsize = *(int *)p;
            if (structsize <= 0)
                break;
            fn(*(pid_t *)&p[72], (const char *)&p[447], arg);
            p += structsize;
        }
    }
    free(buf);
}

/* An older helper still running: stop it, this one takes over. */
static void stop_other(pid_t pid, const char *comm, void *arg)
{
    if (pid != getpid() && !strcmp(comm, NAME)) {
        say("stopping previous helper pid %d", pid);
        kill(pid, SIGKILL);
    }
}

/* Give pid the real root if it is PS5VR. Returns the reply line. */
static const char *unjail(pid_t pid)
{
    app_info_t ai;
    memset(&ai, 0, sizeof ai);
    if (pid <= 0 || sceKernelGetAppInfo(pid, &ai) != 0 || strncmp(ai.title_id, TITLE, 9))
        return "fail: not PS5VR\n";
    const intptr_t root = kernel_get_root_vnode();
    if (!root)
        return "fail: no root vnode\n";
    if (kernel_get_proc_rootdir(pid) == root)
        return "ok\n";
    int r1 = kernel_set_proc_rootdir(pid, root);
    int r2 = kernel_set_proc_jaildir(pid, root);
    say("PS5VR pid %d: real root %s (rootdir %d, jaildir %d)", pid,
        (r1 == 0 && r2 == 0) ? "given" : "FAILED", r1, r2);
    return (r1 == 0 && r2 == 0) ? "ok\n" : "fail: kernel\n";
}

/* Hand tracking: an app may not open the headset cameras (/dev/hmd2_image),
 * this payload may. Give PS5VR this payload's own credentials, one step at a
 * time - 1 capabilities, 2 also attributes, 3 also the auth id - so it gets no
 * more than the cameras need. */
static const char *elevate(pid_t pid, int level)
{
    static char reply[160];
    app_info_t ai;
    memset(&ai, 0, sizeof ai);
    if (pid <= 0 || level < 1 || level > 3 || sceKernelGetAppInfo(pid, &ai) != 0 ||
        strncmp(ai.title_id, TITLE, 9))
        return "fail: not PS5VR\n";
    uint8_t caps[16], attrs[32], mine[32];
    kernel_get_ucred_caps(pid, caps);
    kernel_get_ucred_attrs(pid, attrs);
    const uint64_t authid = kernel_get_ucred_authid(pid);
    int r1 = 0, r2 = 0, r3 = 0;
    kernel_get_ucred_caps(getpid(), mine);
    r1 = kernel_set_ucred_caps(pid, mine);
    if (level >= 2) {
        kernel_get_ucred_attrs(getpid(), mine);
        r2 = kernel_set_ucred_attrs(pid, mine);
    }
    if (level >= 3)
        r3 = kernel_set_ucred_authid(pid, kernel_get_ucred_authid(getpid()));
    snprintf(reply, sizeof reply,
             "%s level %d (was authid 0x%016llx caps %016llx%016llx attrs %02x%02x%02x%02x)\n",
             (r1 | r2 | r3) ? "fail" : "ok", level, (unsigned long long)authid,
             *(unsigned long long *)(caps + 8), *(unsigned long long *)caps, attrs[0], attrs[1],
             attrs[2], attrs[3]);
    say("PS5VR pid %d: %s", pid, reply);
    return reply;
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    for_each_proc(stop_other, NULL);
    /* the payload's own name, so a later copy can find and replace it */
    syscall(0x1d0 /* thr_set_name */, -1, NAME);
    int srv = -1;
    for (int tries = 0; srv < 0 && tries < 20; tries++) {   /* the old copy's port */
        srv = socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        struct sockaddr_in a;
        memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_port = htons(PORT);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(srv, (struct sockaddr *)&a, sizeof a) != 0 || listen(srv, 4) != 0) {
            close(srv);
            srv = -1;
            sleep(1);
        }
    }
    if (srv < 0) {
        say("port %d is taken - not running", PORT);
        return 1;
    }
    say("running: PS5VR (" TITLE ") gets /data and USB access when it asks (127.0.0.1:%d)", PORT);
    for (;;) {
        int c = accept(srv, NULL, NULL);
        if (c < 0)
            continue;
        struct timeval tv = {2, 0};
        setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        char line[64] = {0};
        ssize_t n = recv(c, line, sizeof line - 1, 0);
        int pid = 0;
        const char *reply = "fail: bad request\n";
        int level = 0;
        if (n > 0 && sscanf(line, "unjail %d", &pid) == 1)
            reply = unjail(pid);
        else if (n > 0 && sscanf(line, "elevate %d %d", &pid, &level) == 2)
            reply = elevate(pid, level);
        send(c, reply, strlen(reply), 0);
        close(c);
    }
    return 0;
}
