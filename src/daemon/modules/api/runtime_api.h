/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2019. All rights reserved.
 * iSulad licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: lifeng
 * Create: 2019-11-22
 * Description: provide runtime function definition
 ******************************************************************************/
#ifndef DAEMON_MODULES_API_RUNTIME_API_H
#define DAEMON_MODULES_API_RUNTIME_API_H

#include <stdint.h>
#include <stdbool.h>
#include "isula_libutils/host_config.h"
#include "isula_libutils/oci_runtime_spec.h"
#include "utils.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RUNTIME_NOT_IMPLEMENT_RESET (-2)

typedef enum {
    RUNTIME_CONTAINER_STATUS_UNKNOWN = 0,
    RUNTIME_CONTAINER_STATUS_CREATED = 1,
    RUNTIME_CONTAINER_STATUS_STARTING = 2,
    RUNTIME_CONTAINER_STATUS_RUNNING = 3,
    RUNTIME_CONTAINER_STATUS_STOPPED = 4,
    RUNTIME_CONTAINER_STATUS_PAUSED = 5,
    RUNTIME_CONTAINER_STATUS_RESTARTING = 6,
    RUNTIME_CONTAINER_STATUS_MAX_STATE = 7
} Runtime_Container_Status;

struct runtime_container_status_info {
    bool has_pid;
    uint32_t pid;
    int error_code;
    Runtime_Container_Status status;
};

struct runtime_container_resources_stats_info {
    uint64_t pids_current;
    /* CPU usage */
    uint64_t cpu_use_nanos;
    uint64_t cpu_system_use;
    /* BlkIO usage */
    uint64_t blkio_read;
    uint64_t blkio_write;
    /* Memory usage */
    uint64_t mem_used;
    uint64_t mem_limit;
    uint64_t rss_bytes;
    uint64_t page_faults;
    uint64_t major_page_faults;
    /* Kernel Memory usage */
    uint64_t kmem_used;
    uint64_t kmem_limit;
    /* Cache usage */
    uint64_t cache;
    uint64_t cache_total;
    uint64_t inactive_file_total;
    /* Swap usage*/
    uint64_t swap_used;
    uint64_t swap_limit;
};

typedef struct _rt_create_params_t {
    const char *rootfs;
    const char *bundle;
    const char *state;
    void *oci_config_data;
    bool terminal;
    const char *stdin;
    const char *stdout;
    const char *stderr;
    const char *exit_fifo;
    bool tty;
    bool open_stdin;
    const char *task_addr;
#ifdef ENABLE_NO_PIVOT_ROOT
    bool no_pivot_root;
#endif
} rt_create_params_t;

typedef struct _rt_start_params_t {
    const char *rootpath;
    const char *state;
    const char *bundle;
    bool tty;
    bool open_stdin;

    const char *logpath;
    const char *loglevel;

    const char **console_fifos;

    uint32_t start_timeout;

    const char *container_pidfile;
    const char *exit_fifo;
    bool image_type_oci;
} rt_start_params_t;

typedef struct _rt_kill_params_t {
    const uint32_t signal;
    const uint32_t stop_signal;
    const pid_t pid;
    const unsigned long long start_time;
} rt_kill_params_t;

typedef struct _rt_restart_params_t {
    const char *rootpath;
} rt_restart_params_t;

typedef struct _rt_clean_params_t {
    const char *rootpath;
    const char *statepath;
    const char *logpath;
    const char *loglevel;
    const char *bundle;
    pid_t pid;
} rt_clean_params_t;

typedef struct _rt_rm_params_t {
    const char *rootpath;
} rt_rm_params_t;

typedef struct _rt_status_params_t {
    const char *rootpath;
    const char *state;
    const char *task_address;
} rt_status_params_t;

typedef struct _rt_stats_params_t {
    const char *rootpath;
    const char *state;
} rt_stats_params_t;

typedef struct _rt_exec_params_t {
    const char *rootpath;
    const char *state;
    const char *logpath;
    const char *loglevel;
    const char **console_fifos;
    const char *workdir;
    int64_t timeout;
    const char *suffix;
    defs_process *spec;
    bool attach_stdin;
} rt_exec_params_t;

typedef struct _rt_pause_params_t {
    const char *rootpath;
    const char *state;
} rt_pause_params_t;

typedef struct _rt_resume_params_t {
    const char *rootpath;
    const char *state;
} rt_resume_params_t;

typedef struct _rt_attach_params_t {
    const char *rootpath;
    const char *state;
    const char *stdin;
    const char *stdout;
    const char *stderr;
} rt_attach_params_t;

typedef struct _rt_update_params_t {
    const char *rootpath;
    const host_config *hostconfig;
    const char *state;
} rt_update_params_t;

typedef struct _rt_listpids_params_t {
    const char *state;
    const char *rootpath;
} rt_listpids_params_t;

typedef struct _rt_listpids_out_t {
    pid_t *pids;
    size_t pids_len;
} rt_listpids_out_t;

typedef struct _rt_resize_params_t {
    const char *rootpath;
    unsigned int height;
    unsigned int width;
} rt_resize_params_t;

typedef struct _rt_exec_resize_params_t {
    const char *rootpath;
    const char *state;
    const char *suffix;
    unsigned int height;
    unsigned int width;
} rt_exec_resize_params_t;

typedef struct _rt_runtime_rebuild_config_params_t {
    const char *rootpath;
} rt_rebuild_config_params_t;

typedef struct _rt_runtime_read_pid_ppid_info_params_t {
    int pid;
} rt_read_pid_ppid_info_params_t;

typedef struct _rt_runtime_detect_process_params_t {
    int pid;
    uint64_t start_time;
} rt_detect_process_params_t;

struct rt_ops {
    /* detect whether runtime is of this runtime type */
    bool (*detect)(const char *runtime);

    /* runtime ops */
    int (*rt_create)(const char *name, const char *runtime, const rt_create_params_t *params);

    int (*rt_start)(const char *name, const char *runtime, const rt_start_params_t *params, pid_ppid_info_t *pid_info);

    int (*rt_restart)(const char *name, const char *runtime, const rt_restart_params_t *params);

    int (*rt_kill)(const char *name, const char *runtime, const rt_kill_params_t *params);

    int (*rt_clean_resource)(const char *name, const char *runtime, const rt_clean_params_t *params);

    int (*rt_rm)(const char *name, const char *runtime, const rt_rm_params_t *params);

    int (*rt_status)(const char *name, const char *runtime, const rt_status_params_t *params,
                     struct runtime_container_status_info *status);

    int (*rt_resources_stats)(const char *name, const char *runtime, const rt_stats_params_t *params,
                              struct runtime_container_resources_stats_info *rs_stats);

    int (*rt_exec)(const char *name, const char *runtime, const rt_exec_params_t *params, int *exit_code);

    int (*rt_pause)(const char *name, const char *runtime, const rt_pause_params_t *params);
    int (*rt_resume)(const char *name, const char *runtime, const rt_resume_params_t *params);

    int (*rt_attach)(const char *name, const char *runtime, const rt_attach_params_t *params);

    int (*rt_update)(const char *name, const char *runtime, const rt_update_params_t *params);

    int (*rt_listpids)(const char *name, const char *runtime, const rt_listpids_params_t *params,
                       rt_listpids_out_t *out);
    int (*rt_resize)(const char *name, const char *runtime, const rt_resize_params_t *params);
    int (*rt_exec_resize)(const char *name, const char *runtime, const rt_exec_resize_params_t *params);
    int (*rt_rebuild_config)(const char *name, const char *runtime, const rt_rebuild_config_params_t *params);

    int (*rt_read_pid_ppid_info)(const char *name, const char *runtime, const rt_read_pid_ppid_info_params_t *params,
                                 pid_ppid_info_t *pid_info);
    int (*rt_detect_process)(const char *name, const char *runtime, const rt_detect_process_params_t *params);
};

int runtime_create(const char *name, const char *runtime, const rt_create_params_t *params);
int runtime_clean_resource(const char *name, const char *runtime, const rt_clean_params_t *params);
int runtime_start(const char *name, const char *runtime, const rt_start_params_t *params, pid_ppid_info_t *pid_info);
int runtime_kill(const char *name, const char *runtime, const rt_kill_params_t *params);
int runtime_restart(const char *name, const char *runtime, const rt_restart_params_t *params);
int runtime_rm(const char *name, const char *runtime, const rt_rm_params_t *params);
int runtime_status(const char *name, const char *runtime, const rt_status_params_t *params,
                   struct runtime_container_status_info *status);
int runtime_resources_stats(const char *name, const char *runtime, const rt_stats_params_t *params,
                            struct runtime_container_resources_stats_info *rs_stats);
int runtime_exec(const char *name, const char *runtime, const rt_exec_params_t *params, int *exit_code);
int runtime_pause(const char *name, const char *runtime, const rt_pause_params_t *params);
int runtime_resume(const char *name, const char *runtime, const rt_resume_params_t *params);
int runtime_attach(const char *name, const char *runtime, const rt_attach_params_t *params);

int runtime_update(const char *name, const char *runtime, const rt_update_params_t *params);

int runtime_listpids(const char *name, const char *runtime, const rt_listpids_params_t *params, rt_listpids_out_t *out);
int runtime_rebuild_config(const char *name, const char *runtime, const rt_rebuild_config_params_t *params);
void free_rt_listpids_out_t(rt_listpids_out_t *out);
int runtime_resize(const char *name, const char *runtime, const rt_resize_params_t *params);
int runtime_exec_resize(const char *name, const char *runtime, const rt_exec_resize_params_t *params);

int runtime_read_pid_ppid_info(const char *name, const char *runtime, const rt_read_pid_ppid_info_params_t *params,
                               pid_ppid_info_t *pid_info);
int runtime_detect_process(const char *name, const char *runtime, const rt_detect_process_params_t *params);
bool is_default_runtime(const char *name);

int runtime_init(void);
#ifdef __cplusplus
}
#endif

#endif
