#ifndef APP_BT_CMD_BT_PROCESS_GUARD_H
#define APP_BT_CMD_BT_PROCESS_GUARD_H

#include <nuttx/config.h>

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BT_CMD_PROC_ROOT "/proc"
#define BT_CMD_PROC_MAX 8
#define BT_CMD_POLL_MS 100

static inline bool bt_cmd_is_pid_dir(const char *name)
{
    if (name == NULL || name[0] == '\0')
    {
        return false;
    }

    for (; *name != '\0'; name++)
    {
        if (*name < '0' || *name > '9')
        {
            return false;
        }
    }

    return true;
}

static inline bool bt_cmd_name_matches(const char *cmdline, const char *name)
{
    const char *base;
    const char *end;
    size_t namelen;
    size_t baselen;

    if (cmdline == NULL || name == NULL)
    {
        return false;
    }

    while (*cmdline == ' ' || *cmdline == '\t')
    {
        cmdline++;
    }

    base = strrchr(cmdline, '/');
    base = base != NULL ? base + 1 : cmdline;
    end = base;
    while (*end != '\0' && *end != ' ' && *end != '\t' &&
           *end != '\r' && *end != '\n')
    {
        end++;
    }

    namelen = strlen(name);
    baselen = (size_t)(end - base);
    return baselen == namelen && strncmp(base, name, namelen) == 0;
}

static inline int bt_cmd_read_cmdline(pid_t pid, char *buf, size_t buflen)
{
#ifdef CONFIG_FS_PROCFS
    char path[32];
    ssize_t nread;
    int fd;
    size_t i;

    if (buf == NULL || buflen == 0)
    {
        return -EINVAL;
    }

    snprintf(path, sizeof(path), BT_CMD_PROC_ROOT "/%d/cmdline", (int)pid);
    fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        return -errno;
    }

    nread = read(fd, buf, buflen - 1);
    close(fd);
    if (nread <= 0)
    {
        return nread == 0 ? -ENOENT : -errno;
    }

    for (i = 0; i < (size_t)nread; i++)
    {
        if (buf[i] == '\0')
        {
            buf[i] = ' ';
        }
    }

    buf[nread] = '\0';
    return 0;
#else
    (void)pid;
    (void)buf;
    (void)buflen;
    return -ENOSYS;
#endif
}

static inline int bt_cmd_find_processes(const char *name, pid_t *pids,
                                 size_t max_pids)
{
#ifdef CONFIG_FS_PROCFS
    DIR *dir;
    struct dirent *entry;
    size_t count = 0;

    if (name == NULL || pids == NULL || max_pids == 0)
    {
        return -EINVAL;
    }

    dir = opendir(BT_CMD_PROC_ROOT);
    if (dir == NULL)
    {
        return -errno;
    }

    while ((entry = readdir(dir)) != NULL)
    {
        char cmdline[80];
        pid_t pid;

        if (!bt_cmd_is_pid_dir(entry->d_name))
        {
            continue;
        }

        pid = (pid_t)atoi(entry->d_name);
        if (pid <= 0 || bt_cmd_read_cmdline(pid, cmdline,
                                            sizeof(cmdline)) < 0)
        {
            continue;
        }

        if (bt_cmd_name_matches(cmdline, name))
        {
            pids[count++] = pid;
            if (count >= max_pids)
            {
                break;
            }
        }
    }

    closedir(dir);
    return (int)count;
#else
    (void)name;
    (void)pids;
    (void)max_pids;
    return -ENOSYS;
#endif
}

static inline int bt_cmd_count_processes(const char *name)
{
    pid_t pids[BT_CMD_PROC_MAX];

    return bt_cmd_find_processes(name, pids,
                                 sizeof(pids) / sizeof(pids[0]));
}

static inline int bt_cmd_wait_no_processes(const char *name,
                                           unsigned int wait_ms)
{
    int count;

    for (;;)
    {
        count = bt_cmd_count_processes(name);
        if (count <= 0)
        {
            return count;
        }

        if (wait_ms == 0)
        {
            return count;
        }

        usleep(BT_CMD_POLL_MS * 1000);
        wait_ms = wait_ms > BT_CMD_POLL_MS ? wait_ms - BT_CMD_POLL_MS : 0;
    }
}

static inline int bt_cmd_signal_processes(const char *name, int signo)
{
    pid_t pids[BT_CMD_PROC_MAX];
    int count;
    int signaled = 0;
    int i;

    count = bt_cmd_find_processes(name, pids,
                                  sizeof(pids) / sizeof(pids[0]));
    if (count <= 0)
    {
        return count;
    }

    for (i = 0; i < count; i++)
    {
        if (kill(pids[i], signo) == 0)
        {
            signaled++;
        }
    }

    return signaled;
}

static inline int bt_cmd_terminate_processes(const char *name,
                                             unsigned int term_wait_ms,
                                             unsigned int kill_wait_ms)
{
    int count;
    int signaled;
    int alive;

    count = bt_cmd_count_processes(name);
    if (count <= 0)
    {
        return count;
    }

    signaled = bt_cmd_signal_processes(name, SIGTERM);
    if (signaled < 0)
    {
        return signaled;
    }

    alive = bt_cmd_wait_no_processes(name, term_wait_ms);
    if (alive <= 0)
    {
        return count;
    }

#ifdef SIGKILL
    signaled = bt_cmd_signal_processes(name, SIGKILL);
    if (signaled < 0)
    {
        return signaled;
    }

    alive = bt_cmd_wait_no_processes(name, kill_wait_ms);
    if (alive <= 0)
    {
        return count;
    }
#else
    (void)kill_wait_ms;
#endif

    return -EBUSY;
}

#endif
