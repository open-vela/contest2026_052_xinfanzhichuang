/*
 * Copyright (C) 2026 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "config_store.h"
#include "velaclaw_config.h"
#include "velaclaw_compat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include "cJSON.h"

static const char *TAG = "cfgstore";

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;

/* ── helpers ─────────────────────────────────────────────────── */

static int mkdirs(const char *path)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    mkdir(tmp, 0700);
    return OK;
}

static cJSON *load_json(void)
{
    FILE *f = fopen(VELACLAW_CONFIG_FILE, "r");
    if (!f) return cJSON_CreateObject();

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0) { fclose(f); return cJSON_CreateObject(); }

    char *buf = (char *)malloc((size_t)(sz + 1));
    if (!buf) { fclose(f); return cJSON_CreateObject(); }

    size_t n = fread(buf, 1, (size_t)sz, f);
    buf[n] = '\0';
    fclose(f);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    return root ? root : cJSON_CreateObject();
}

static int write_all(int fd, const char *buf, size_t len)
{
    size_t off = 0;

    while (off < len) {
        ssize_t n = write(fd, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return ERROR;
        }
        if (n == 0) {
            return ERROR;
        }
        off += (size_t)n;
    }

    return OK;
}

static int seed_default_config(void)
{
    struct stat st;
    char tmp_path[sizeof(VELACLAW_CONFIG_FILE) + 8];
    FILE *src;
    int fd;
    int ret = ERROR;
    char buf[256];
    size_t n;

    if (stat(VELACLAW_CONFIG_FILE, &st) == 0 && st.st_size > 0) {
        return OK;
    }

    src = fopen(VELACLAW_DEFAULT_CONFIG_FILE, "r");
    if (!src) {
        syslog(LOG_INFO, "[%s] No bundled default config at %s\n",
            TAG, VELACLAW_DEFAULT_CONFIG_FILE);
        return ERROR;
    }

    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", VELACLAW_CONFIG_FILE);
    fd = open(tmp_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        fclose(src);
        return ERROR;
    }

    while ((n = fread(buf, 1, sizeof(buf), src)) > 0) {
        if (write_all(fd, buf, n) != OK) {
            goto out;
        }
    }

    if (ferror(src) != 0 || fsync(fd) < 0) {
        goto out;
    }

    if (close(fd) < 0) {
        fd = -1;
        goto out;
    }
    fd = -1;

    if (rename(tmp_path, VELACLAW_CONFIG_FILE) < 0) {
        goto out;
    }

    ret = OK;
    syslog(LOG_INFO, "[%s] Seeded default config from %s\n",
        TAG, VELACLAW_DEFAULT_CONFIG_FILE);

out:
    if (fd >= 0) {
        close(fd);
    }
    fclose(src);
    if (ret != OK) {
        unlink(tmp_path);
    }
    return ret;
}

static int save_json(cJSON *root)
{
    char *str = cJSON_PrintUnformatted(root);
    if (!str) return ERROR;

    /* Use open() with explicit 0600 to ensure config file is owner-only.
     * fopen("w") inherits umask which may be too permissive. */
    int fd = open(VELACLAW_CONFIG_FILE,
                  O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) { free(str); return ERROR; }

    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); free(str); return ERROR; }
    fputs(str, f);
    fclose(f);
    free(str);
    return OK;
}

/* ── public API ──────────────────────────────────────────────── */

int config_store_init(void)
{
    mkdirs(VELACLAW_DATA_DIR);
    mkdirs(VELACLAW_CONFIG_DIR);
    mkdirs(VELACLAW_MEMORY_DIR);
    mkdirs(VELACLAW_SESSION_DIR);
    if (seed_default_config() != OK) {
        syslog(LOG_WARNING, "[%s] Default config was not installed\n", TAG);
    }
    syslog(LOG_INFO, "[%s] Config store ready at %s\n", TAG, VELACLAW_CONFIG_FILE);
    return OK;
}

int claw_config_get(const char *key, char *buf, size_t buf_size)
{
    pthread_mutex_lock(&s_lock);
    cJSON *root = load_json();
    cJSON *item = cJSON_GetObjectItem(root, key);
    int ret = ERROR;
    if (item && cJSON_IsString(item) && item->valuestring[0] != '\0') {
        strncpy(buf, item->valuestring, buf_size - 1);
        buf[buf_size - 1] = '\0';
        ret = OK;
    }
    cJSON_Delete(root);
    pthread_mutex_unlock(&s_lock);
    return ret;
}

int claw_config_set(const char *key, const char *value)
{
    pthread_mutex_lock(&s_lock);
    cJSON *root = load_json();
    cJSON_DeleteItemFromObject(root, key);
    cJSON_AddStringToObject(root, key, value);
    int ret = save_json(root);
    cJSON_Delete(root);
    pthread_mutex_unlock(&s_lock);
    return ret;
}

int config_del(const char *key)
{
    pthread_mutex_lock(&s_lock);
    cJSON *root = load_json();
    cJSON_DeleteItemFromObject(root, key);
    int ret = save_json(root);
    cJSON_Delete(root);
    pthread_mutex_unlock(&s_lock);
    return ret;
}

int config_erase_all(void)
{
    pthread_mutex_lock(&s_lock);
    cJSON *empty = cJSON_CreateObject();
    int ret = save_json(empty);
    cJSON_Delete(empty);
    pthread_mutex_unlock(&s_lock);
    return ret;
}
