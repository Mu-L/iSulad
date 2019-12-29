/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2017-2019. All rights reserved.
 * iSulad licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan PSL v1.
 * You may obtain a copy of Mulan PSL v1 at:
 *     http://license.coscl.org.cn/MulanPSL
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v1 for more details.
 * Author: tanyifeng
 * Create: 2017-11-22
 * Description: provide container specs functions
 ******************************************************************************/
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/utsname.h>
#include <sched.h>
#include <ctype.h>

#include "error.h"
#include "log.h"
#include "specs.h"
#include "oci_runtime_spec.h"
#include "oci_runtime_hooks.h"
#include "docker_seccomp.h"
#include "host_config.h"
#include "container_custom_config.h"
#include "utils.h"
#include "config.h"
#include "lcrd_config.h"
#include "namespace.h"
#include "specs_security.h"
#include "specs_mount.h"
#include "specs_extend.h"
#include "image.h"
#include "path.h"

#ifndef CLONE_NEWUTS
#define CLONE_NEWUTS            0x04000000
#endif

#ifndef CLONE_NEWUSER
#define CLONE_NEWUSER           0x10000000
#endif

#ifndef CLONE_NEWNET
#define CLONE_NEWNET            0x40000000
#endif

#ifndef CLONE_NEWNS
#define CLONE_NEWNS             0x00020000
#endif

#ifndef CLONE_NEWPID
#define CLONE_NEWPID            0x20000000
#endif

#ifndef CLONE_NEWIPC
#define CLONE_NEWIPC            0x08000000
#endif

#ifndef CLONE_NEWCGROUP
#define CLONE_NEWCGROUP         0x02000000
#endif

#define OCICONFIGJSON "ociconfig.json"

static int merge_annotations(oci_runtime_spec *oci_spec, container_custom_config *custom_conf)
{
    int ret = 0;
    size_t i;

    if (custom_conf->annotations != NULL && custom_conf->annotations->len) {
        if (oci_spec->annotations->len > LIST_SIZE_MAX - custom_conf->annotations->len) {
            ERROR("Too many annotations to add, the limit is %d", LIST_SIZE_MAX);
            lcrd_set_error_message("Too many annotations to add, the limit is %d", LIST_SIZE_MAX);
            ret = -1;
            goto out;
        }
        for (i = 0; i < custom_conf->annotations->len; i++) {
            ret = append_json_map_string_string(oci_spec->annotations, custom_conf->annotations->keys[i],
                                                custom_conf->annotations->values[i]);
            if (ret != 0) {
                ERROR("Failed to append annotation:%s, value:%s", custom_conf->annotations->keys[i],
                      custom_conf->annotations->values[i]);
                goto out;
            }
        }
    }
out:
    return ret;
}

static int make_annotations_log_console(const oci_runtime_spec *oci_spec, const container_custom_config *custom_conf)
{
    int ret = 0;
    int nret = 0;
    char tmp_str[LCRD_NUMSTRLEN64] = {0};

    if (custom_conf->log_config != NULL) {
        if (custom_conf->log_config->log_file != NULL) {
            if (append_json_map_string_string(oci_spec->annotations, CONTAINER_LOG_CONFIG_KEY_FILE,
                                              custom_conf->log_config->log_file)) {
                ERROR("append log console file failed");
                ret = -1;
                goto out;
            }
        }

        nret = snprintf(tmp_str, sizeof(tmp_str), "%llu",
                        (unsigned long long)(custom_conf->log_config->log_file_rotate));
        if (nret < 0 || (size_t)nret >= sizeof(tmp_str)) {
            ERROR("create rotate string failed");
            ret = -1;
            goto out;
        }

        if (append_json_map_string_string(oci_spec->annotations, CONTAINER_LOG_CONFIG_KEY_ROTATE, tmp_str)) {
            ERROR("append log console file rotate failed");
            ret = -1;
            goto out;
        }

        if (custom_conf->log_config->log_file_size != NULL) {
            if (append_json_map_string_string(oci_spec->annotations, CONTAINER_LOG_CONFIG_KEY_SIZE,
                                              custom_conf->log_config->log_file_size)) {
                ERROR("append log console file size failed");
                ret = -1;
                goto out;
            }
        }
    }

out:
    return ret;
}

static int make_annotations_network_mode(const oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = 0;

    if (host_spec->network_mode != NULL) {
        if (append_json_map_string_string(oci_spec->annotations, "host.network.mode", host_spec->network_mode)) {
            ERROR("append network mode failed");
            ret = -1;
            goto out;
        }
    }

out:
    return ret;
}

static int make_annotations_system_container(const oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = 0;

    if (host_spec->system_container) {
        if (append_json_map_string_string(oci_spec->annotations, "system.container", "true")) {
            ERROR("Realloc annotations failed");
            ret = -1;
            goto out;
        }
    }

out:
    return ret;
}

static int make_annotations_cgroup_dir(const oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = 0;
    char cleaned[PATH_MAX] = { 0 };
    char *default_cgroup_parent = NULL;
    char *path = NULL;

    default_cgroup_parent = conf_get_lcrd_cgroup_parent();
    if (host_spec->cgroup_parent != NULL) {
        path = host_spec->cgroup_parent;
    } else if (default_cgroup_parent != NULL) {
        path = default_cgroup_parent;
    }
    if (path == NULL) {
        goto out;
    }
    if (cleanpath(path, cleaned, sizeof(cleaned)) == NULL) {
        ERROR("Failed to clean path: %s", path);
        ret = -1;
        goto out;
    }
    if (append_json_map_string_string(oci_spec->annotations, "cgroup.dir", cleaned)) {
        ERROR("Realloc annotations failed");
        ret = -1;
        goto out;
    }

out:
    free(default_cgroup_parent);
    return ret;
}

static int make_annotations_oom_score_adj(const oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = 0;
    char tmp_str[LCRD_NUMSTRLEN64 + 1] = { 0 };

    // oom_score_adj default value is 0, So there is no need to explicitly set this value
    if (host_spec->oom_score_adj != 0) {
        int nret = snprintf(tmp_str, sizeof(tmp_str), "%d", host_spec->oom_score_adj);
        if (nret < 0 || (size_t)nret >= sizeof(tmp_str)) {
            ERROR("create oom score adj string failed");
            ret = -1;
            goto out;
        }
        if (append_json_map_string_string(oci_spec->annotations, "proc.oom_score_adj", tmp_str)) {
            ERROR("append oom score adj which configure proc filesystem for the container failed ");
            ret = -1;
            goto out;
        }
    }

out:
    return ret;
}

static int make_annotations_files_limit(const oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = 0;
    char tmp_str[LCRD_NUMSTRLEN64 + 1] = { 0 };

    // Not supported in oci runtime-spec, add 'files.limit' to annotations
    if (host_spec->files_limit != 0) {
        // need create new file limit item in annotations
        int64_t filelimit = host_spec->files_limit;
        int nret = snprintf(tmp_str, sizeof(tmp_str), "%lld", (long long)filelimit);
        if (nret < 0 || (size_t)nret >= sizeof(tmp_str)) {
            ERROR("create files limit string failed");
            ret = -1;
            goto out;
        }

        if (append_json_map_string_string(oci_spec->annotations, "files.limit", tmp_str)) {
            ERROR("append files limit failed");
            ret = -1;
            goto out;
        }
    }

out:
    return ret;
}

static int make_sure_oci_spec_annotations(oci_runtime_spec *oci_spec)
{
    if (oci_spec->annotations == NULL) {
        oci_spec->annotations = util_common_calloc_s(sizeof(json_map_string_string));
        if (oci_spec->annotations == NULL) {
            return -1;
        }
    }
    return 0;
}

static int make_annotations(oci_runtime_spec *oci_spec, container_custom_config *custom_conf, host_config *host_spec)
{
    int ret = 0;

    ret = make_sure_oci_spec_annotations(oci_spec);
    if (ret < 0) {
        goto out;
    }

    ret = make_annotations_network_mode(oci_spec, host_spec);
    if (ret != 0) {
        ret = -1;
        goto out;
    }

    ret = make_annotations_system_container(oci_spec, host_spec);
    if (ret != 0) {
        ret = -1;
        goto out;
    }

    ret = make_annotations_cgroup_dir(oci_spec, host_spec);
    if (ret != 0) {
        ret = -1;
        goto out;
    }

    ret = make_annotations_oom_score_adj(oci_spec, host_spec);
    if (ret != 0) {
        ret = -1;
        goto out;
    }

    ret = make_annotations_files_limit(oci_spec, host_spec);
    if (ret != 0) {
        ret = -1;
        goto out;
    }

    ret = make_annotations_log_console(oci_spec, custom_conf);
    if (ret != 0) {
        ret = -1;
        goto out;
    }

    if (merge_annotations(oci_spec, custom_conf)) {
        ret = -1;
        goto out;
    }

out:
    return ret;
}

/* default_spec returns default oci spec used by lcrd. */
oci_runtime_spec *default_spec(bool system_container)
{
    const char *oci_file = OCICONFIG_PATH;
    if (system_container) {
        oci_file = OCI_SYSTEM_CONTAINER_CONFIG_PATH;
    }
    oci_runtime_spec *oci_spec = NULL;
    parser_error err = NULL;

    /* parse the input oci file */
    oci_spec = oci_runtime_spec_parse_file(oci_file, NULL, &err);
    if (oci_spec == NULL) {
        ERROR("Failed to parse OCI specification file \"%s\", error message: %s", oci_file, err);
        lcrd_set_error_message("Can not read the default /etc/default/lcrd/config.json file: %s", err);
        goto out;
    }

out:
    free(err);
    return oci_spec;
}

static int make_sure_oci_spec_root(oci_runtime_spec *oci_spec)
{
    if (oci_spec->root == NULL) {
        oci_spec->root = util_common_calloc_s(sizeof(oci_runtime_spec_root));
        if (oci_spec->root == NULL) {
            return -1;
        }
    }
    return 0;
}

static int merge_root(oci_runtime_spec *oci_spec, const char *rootfs, const host_config *host_spec)
{
    int ret = 0;

    ret = make_sure_oci_spec_root(oci_spec);
    if (ret < 0) {
        goto out;
    }

    // fill root path properties
    if (rootfs != NULL) {
        free(oci_spec->root->path);
        oci_spec->root->path = util_strdup_s(rootfs);
    }
    if (host_spec->readonly_rootfs) {
        oci_spec->root->readonly = host_spec->readonly_rootfs;
    }

out:
    return ret;
}

static int merge_blkio_weight(oci_runtime_spec *oci_spec, uint16_t blkio_weight)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources_blkio(oci_spec);
    if (ret < 0) {
        goto out;
    }

    oci_spec->linux->resources->block_io->weight = blkio_weight;

out:
    return ret;
}

static int make_sure_oci_spec_linux_resources_cpu(oci_runtime_spec *oci_spec)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources(oci_spec);
    if (ret < 0) {
        return -1;
    }

    if (oci_spec->linux->resources->cpu == NULL) {
        oci_spec->linux->resources->cpu = util_common_calloc_s(sizeof(oci_runtime_config_linux_resources_cpu));
        if (oci_spec->linux->resources->cpu == NULL) {
            return -1;
        }
    }
    return 0;
}

static int merge_cpu_shares(oci_runtime_spec *oci_spec, int64_t cpu_shares)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources_cpu(oci_spec);
    if (ret < 0) {
        goto out;
    }

    oci_spec->linux->resources->cpu->shares = (uint64_t)cpu_shares;

out:
    return ret;
}

static int merge_cpu_period(oci_runtime_spec *oci_spec, int64_t cpu_period)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources_cpu(oci_spec);
    if (ret < 0) {
        goto out;
    }

    oci_spec->linux->resources->cpu->period = (uint64_t)cpu_period;

out:
    return ret;
}

static int merge_cpu_realtime_period(oci_runtime_spec *oci_spec, int64_t cpu_rt_period)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources_cpu(oci_spec);
    if (ret < 0) {
        goto out;
    }

    oci_spec->linux->resources->cpu->realtime_period = (uint64_t)cpu_rt_period;

out:
    return ret;
}

static int merge_cpu_realtime_runtime(oci_runtime_spec *oci_spec, int64_t cpu_rt_runtime)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources_cpu(oci_spec);
    if (ret < 0) {
        goto out;
    }

    oci_spec->linux->resources->cpu->realtime_runtime = cpu_rt_runtime;

out:
    return ret;
}

static int merge_cpu_quota(oci_runtime_spec *oci_spec, int64_t cpu_quota)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources_cpu(oci_spec);
    if (ret < 0) {
        goto out;
    }

    oci_spec->linux->resources->cpu->quota = cpu_quota;

out:
    return ret;
}

static int merge_cpuset_cpus(oci_runtime_spec *oci_spec, const char *cpuset_cpus)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources_cpu(oci_spec);
    if (ret < 0) {
        goto out;
    }

    free(oci_spec->linux->resources->cpu->cpus);
    oci_spec->linux->resources->cpu->cpus = util_strdup_s(cpuset_cpus);

out:
    return ret;
}

static int merge_cpuset_mems(oci_runtime_spec *oci_spec, const char *cpuset_mems)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources_cpu(oci_spec);
    if (ret < 0) {
        goto out;
    }

    free(oci_spec->linux->resources->cpu->mems);
    oci_spec->linux->resources->cpu->mems = util_strdup_s(cpuset_mems);

out:
    return ret;
}

static int make_sure_oci_spec_linux_resources_mem(oci_runtime_spec *oci_spec)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources(oci_spec);
    if (ret < 0) {
        return -1;
    }

    if (oci_spec->linux->resources->memory == NULL) {
        oci_spec->linux->resources->memory = util_common_calloc_s(sizeof(oci_runtime_config_linux_resources_memory));
        if (oci_spec->linux->resources->memory == NULL) {
            return -1;
        }
    }
    return 0;
}

static int merge_memory_limit(oci_runtime_spec *oci_spec, int64_t memory)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources_mem(oci_spec);
    if (ret < 0) {
        goto out;
    }

    oci_spec->linux->resources->memory->limit = memory;

out:
    return ret;
}

static int merge_memory_oom_kill_disable(oci_runtime_spec *oci_spec, bool oom_kill_disable)
{
    int ret = 0;

    if (oci_spec->linux == NULL) {
        oci_spec->linux = util_common_calloc_s(sizeof(oci_runtime_config_linux));
        if (oci_spec->linux == NULL) {
            ret = -1;
            goto out;
        }
    }

    if (oci_spec->linux->resources == NULL) {
        oci_spec->linux->resources = util_common_calloc_s(sizeof(oci_runtime_config_linux_resources));
        if (oci_spec->linux->resources == NULL) {
            ret = -1;
            goto out;
        }
    }

    if (oci_spec->linux->resources->memory == NULL) {
        oci_spec->linux->resources->memory = util_common_calloc_s(sizeof(oci_runtime_config_linux_resources_memory));
        if (oci_spec->linux->resources->memory == NULL) {
            ret = -1;
            goto out;
        }
    }

    oci_spec->linux->resources->memory->disable_oom_killer = oom_kill_disable;

out:
    return ret;
}

static int merge_memory_swap(oci_runtime_spec *oci_spec, int64_t memory_swap)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources_mem(oci_spec);
    if (ret < 0) {
        goto out;
    }

    oci_spec->linux->resources->memory->swap = memory_swap;

out:
    return ret;
}

static int merge_memory_reservation(oci_runtime_spec *oci_spec, int64_t memory_reservation)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources_mem(oci_spec);
    if (ret < 0) {
        goto out;
    }

    oci_spec->linux->resources->memory->reservation = memory_reservation;

out:
    return ret;
}

static int merge_kernel_memory(oci_runtime_spec *oci_spec, int64_t kernel_memory)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources_mem(oci_spec);
    if (ret < 0) {
        goto out;
    }

    oci_spec->linux->resources->memory->kernel = kernel_memory;

out:
    return ret;
}

static int merge_hugetlbs(oci_runtime_spec *oci_spec, host_config_hugetlbs_element **hugetlbs, size_t hugetlbs_len)
{
    int ret = 0;
    size_t i = 0;
    size_t new_size, old_size;
    oci_runtime_config_linux_resources_hugepage_limits_element **hugepage_limits_temp = NULL;

    ret = make_sure_oci_spec_linux_resources(oci_spec);
    if (ret < 0) {
        goto out;
    }

    if (hugetlbs_len > SIZE_MAX / sizeof(oci_runtime_config_linux_resources_hugepage_limits_element *) -
        oci_spec->linux->resources->hugepage_limits_len) {
        ERROR("Too many hugetlbs to merge!");
        ret = -1;
        goto out;
    }
    old_size = oci_spec->linux->resources->hugepage_limits_len *
               sizeof(oci_runtime_config_linux_resources_hugepage_limits_element *);
    new_size = (oci_spec->linux->resources->hugepage_limits_len + hugetlbs_len)
               * sizeof(oci_runtime_config_linux_resources_hugepage_limits_element *);
    ret = mem_realloc((void **)&hugepage_limits_temp, new_size,
                      oci_spec->linux->resources->hugepage_limits,  old_size);
    if (ret != 0) {
        ERROR("Failed to realloc memory for hugepage limits");
        ret = -1;
        goto out;
    }

    oci_spec->linux->resources->hugepage_limits = hugepage_limits_temp;

    for (i = 0; i < hugetlbs_len; i++) {
        oci_spec->linux->resources->hugepage_limits[oci_spec->linux->resources->hugepage_limits_len]
            = util_common_calloc_s(sizeof(oci_runtime_config_linux_resources_hugepage_limits_element));
        if (oci_spec->linux->resources->hugepage_limits[oci_spec->linux->resources->hugepage_limits_len] == NULL) {
            ERROR("Failed to malloc memory for hugepage limits");
            ret = -1;
            goto out;
        }
        oci_spec->linux->resources->hugepage_limits[oci_spec->linux->resources->hugepage_limits_len]->limit
            = hugetlbs[i]->limit;
        oci_spec->linux->resources->hugepage_limits[oci_spec->linux->resources->hugepage_limits_len]->page_size
            = util_strdup_s(hugetlbs[i]->page_size);
        oci_spec->linux->resources->hugepage_limits_len++;
    }
out:
    return ret;
}

static int make_sure_oci_spec_hooks(oci_runtime_spec *oci_spec)
{
    if (oci_spec->hooks == NULL) {
        oci_spec->hooks = util_common_calloc_s(sizeof(oci_runtime_spec_hooks));
        if (oci_spec->hooks == NULL) {
            return -1;
        }
    }
    return 0;
}

static int merge_hook_spec(oci_runtime_spec *oci_spec, const char *hook_spec)
{
    int ret = 0;
    parser_error err = NULL;
    oci_runtime_spec_hooks *hooks = NULL;

    if (hook_spec == NULL) {
        return 0;
    }

    ret = make_sure_oci_spec_hooks(oci_spec);
    if (ret < 0) {
        goto out;
    }

    hooks = oci_runtime_spec_hooks_parse_file(hook_spec, NULL, &err);
    if (hooks == NULL) {
        ERROR("Failed to parse hook-spec file: %s", err);
        ret = -1;
        goto out;
    }
    ret = merge_hooks(oci_spec->hooks, hooks);
    free_oci_runtime_spec_hooks(hooks);
    if (ret < 0) {
        goto out;
    }

out:
    free(err);
    return ret;
}

static void clean_correlated_selinux(oci_runtime_spec_process *process)
{
    if (process == NULL) {
        return;
    }

    free(process->selinux_label);
    process->selinux_label = NULL;
}

static void clean_correlated_read_only_path(oci_runtime_config_linux *linux)
{
    if (linux == NULL) {
        return;
    }

    if (linux->readonly_paths != NULL && linux->readonly_paths_len) {
        size_t i;
        for (i = 0; i < linux->readonly_paths_len; i++) {
            free(linux->readonly_paths[i]);
            linux->readonly_paths[i] = NULL;
        }
        free(linux->readonly_paths);
        linux->readonly_paths = NULL;
        linux->readonly_paths_len = 0;
    }
}

static void clean_correlated_masked_path(oci_runtime_config_linux *linux)
{
    if (linux == NULL) {
        return;
    }

    if (linux->masked_paths != NULL && linux->masked_paths_len) {
        size_t i;
        for (i = 0; i < linux->masked_paths_len; i++) {
            free(linux->masked_paths[i]);
            linux->masked_paths[i] = NULL;
        }
        free(linux->masked_paths);
        linux->masked_paths = NULL;
        linux->masked_paths_len = 0;
    }
}

static void clean_correlated_seccomp(oci_runtime_config_linux *linux)
{
    if (linux == NULL) {
        return;
    }

    free_oci_runtime_config_linux_seccomp(linux->seccomp);
    linux->seccomp = NULL;
}

static void clean_correlated_items(const oci_runtime_spec *oci_spec)
{
    if (oci_spec == NULL) {
        return;
    }

    clean_correlated_selinux(oci_spec->process);
    clean_correlated_masked_path(oci_spec->linux);
    clean_correlated_read_only_path(oci_spec->linux);
    clean_correlated_seccomp(oci_spec->linux);
}

static int adapt_settings_for_privileged(oci_runtime_spec *oci_spec, bool privileged)
{
    int ret = 0;
    size_t all_caps_len = 0;

    if (!privileged) {
        return 0;
    }

    all_caps_len = util_get_all_caps_len();
    if (oci_spec == NULL) {
        ret = -1;
        goto out;
    }

    clean_correlated_items(oci_spec);

    ret = set_mounts_readwrite_option(oci_spec);
    if (ret != 0) {
        goto out;
    }

    /* add all capabilities */
    ret = make_sure_oci_spec_process(oci_spec);
    if (ret < 0) {
        goto out;
    }

    ret = refill_oci_process_capabilities(&oci_spec->process->capabilities, g_all_caps, all_caps_len);
    if (ret != 0) {
        ERROR("Failed to copy all capabilities");
        ret = -1;
        goto out;
    }

    ret = merge_all_devices_and_all_permission(oci_spec);
    if (ret != 0) {
        ERROR("Failed to merge all devices on host and all devices's cgroup permission");
        ret = -1;
        goto out;
    }

out:
    return ret;
}

static int make_sure_oci_spec_linux_resources_pids(oci_runtime_spec *oci_spec)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources(oci_spec);
    if (ret < 0) {
        return -1;
    }

    if (oci_spec->linux->resources->pids == NULL) {
        oci_spec->linux->resources->pids = util_common_calloc_s(sizeof(oci_runtime_config_linux_resources_pids));
        if (oci_spec->linux->resources->pids == NULL) {
            return -1;
        }
    }
    return 0;
}

static int merge_pids_limit(oci_runtime_spec *oci_spec, int64_t pids_limit)
{
    int ret = 0;

    ret = make_sure_oci_spec_linux_resources_pids(oci_spec);
    if (ret < 0) {
        goto out;
    }

    oci_spec->linux->resources->pids->limit = pids_limit;

out:
    return ret;
}

static int merge_hostname(oci_runtime_spec *oci_spec, const host_config *host_spec,
                          container_custom_config *custom_spec)
{
    int ret = 0;

    if (custom_spec->hostname == NULL) {
        if (host_spec->network_mode != NULL && !strcmp(host_spec->network_mode, "host")) {
            char hostname[MAX_HOST_NAME_LEN] = { 0x00 };
            ret = gethostname(hostname, sizeof(hostname));
            if (ret != 0) {
                ERROR("Get hostname error");
                goto out;
            }
            custom_spec->hostname = util_strdup_s(hostname);
        } else {
            custom_spec->hostname = util_strdup_s("localhost");
        }
    }

    free(oci_spec->hostname);
    oci_spec->hostname = util_strdup_s(custom_spec->hostname);
out:
    return ret;
}

static int merge_conf_cgroup_cpu_int64(oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = 0;

    /* cpu shares */
    if (host_spec->cpu_shares != 0) {
        ret = merge_cpu_shares(oci_spec, host_spec->cpu_shares);
        if (ret != 0) {
            ERROR("Failed to merge cgroup cpu shares");
            goto out;
        }
    }

    /* cpu period */
    if (host_spec->cpu_period != 0) {
        ret = merge_cpu_period(oci_spec, host_spec->cpu_period);
        if (ret != 0) {
            ERROR("Failed to merge cgroup cpu period");
            goto out;
        }
    }

    /* cpu realtime period */
    if (host_spec->cpu_realtime_period != 0) {
        ret = merge_cpu_realtime_period(oci_spec, host_spec->cpu_realtime_period);
        if (ret != 0) {
            ERROR("Failed to merge cgroup cpu realtime period");
            goto out;
        }
    }

    /* cpu realtime runtime */
    if (host_spec->cpu_realtime_runtime != 0) {
        ret = merge_cpu_realtime_runtime(oci_spec, host_spec->cpu_realtime_runtime);
        if (ret != 0) {
            ERROR("Failed to merge cgroup cpu realtime runtime");
            goto out;
        }
    }

    /* cpu quota */
    if (host_spec->cpu_quota != 0) {
        ret = merge_cpu_quota(oci_spec, host_spec->cpu_quota);
        if (ret != 0) {
            ERROR("Failed to merge cgroup cpu quota");
            goto out;
        }
    }

out:
    return ret;
}

static int merge_conf_cgroup_cpu(oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = 0;

    ret = merge_conf_cgroup_cpu_int64(oci_spec, host_spec);
    if (ret != 0) {
        goto out;
    }

    /* cpuset-cpus */
    if (util_valid_str(host_spec->cpuset_cpus)) {
        ret = merge_cpuset_cpus(oci_spec, host_spec->cpuset_cpus);
        if (ret != 0) {
            ERROR("Failed to merge cgroup cpuset cpus");
            goto out;
        }
    }

    /* cpuset mems */
    if (util_valid_str(host_spec->cpuset_mems)) {
        ret = merge_cpuset_mems(oci_spec, host_spec->cpuset_mems);
        if (ret != 0) {
            ERROR("Failed to merge cgroup cpuset mems");
            goto out;
        }
    }

out:
    return ret;
}

static int merge_conf_cgroup_memory(oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = 0;

    /* memory limit */
    if (host_spec->memory != 0) {
        ret = merge_memory_limit(oci_spec, host_spec->memory);
        if (ret != 0) {
            ERROR("Failed to merge cgroup memory limit");
            goto out;
        }
    }

    if (host_spec->oom_kill_disable) {
        if (host_spec->memory == 0) {
            WARN("Disabling the OOM killer on containers without setting memory limit may be dangerous.");
        }

        ret = merge_memory_oom_kill_disable(oci_spec, host_spec->oom_kill_disable);
        if (ret != 0) {
            ERROR("Failed to merge cgroup memory oom kill disable");
            goto out;
        }
    }

    /* memory swap */
    if (host_spec->memory_swap != 0) {
        ret = merge_memory_swap(oci_spec, host_spec->memory_swap);
        if (ret != 0) {
            ERROR("Failed to merge cgroup memory swap");
            goto out;
        }
    }

    /* memory reservation */
    if (host_spec->memory_reservation != 0) {
        ret = merge_memory_reservation(oci_spec, host_spec->memory_reservation);
        if (ret != 0) {
            ERROR("Failed to merge cgroup memory reservation");
            goto out;
        }
    }

    /* kernel_memory */
    if (host_spec->kernel_memory != 0) {
        ret = merge_kernel_memory(oci_spec, host_spec->kernel_memory);
        if (ret != 0) {
            ERROR("Failed to merge cgroup kernel_memory");
            goto out;
        }
    }

out:
    return ret;
}

static int merge_conf_blkio_weight(oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = 0;

    /* blkio weight */
    if (host_spec->blkio_weight != 0) {
        ret = merge_blkio_weight(oci_spec, host_spec->blkio_weight);
        if (ret != 0) {
            ERROR("Failed to merge cgroup blkio weight");
            goto out;
        }
    }
out:
    return ret;
}

static int do_merge_one_ulimit_override(const oci_runtime_spec *oci_spec,
                                        oci_runtime_spec_process_rlimits_element *rlimit)
{
    size_t j;
    bool exists = false;

    for (j = 0; j < oci_spec->process->rlimits_len; j++) {
        if (oci_spec->process->rlimits[j]->type == NULL) {
            ERROR("rlimit type is empty");
            free(rlimit->type);
            free(rlimit);
            return -1;
        }
        if (strcmp(oci_spec->process->rlimits[j]->type, rlimit->type) == 0) {
            exists = true;
            break;
        }
    }
    if (exists) {
        /* override ulimit */
        free_oci_runtime_spec_process_rlimits_element(oci_spec->process->rlimits[j]);
        oci_spec->process->rlimits[j] = rlimit;
    } else {
        oci_spec->process->rlimits[oci_spec->process->rlimits_len] = rlimit;
        oci_spec->process->rlimits_len++;
    }

    return 0;
}

static int merge_one_ulimit_override(const oci_runtime_spec *oci_spec, const host_config_ulimits_element *ulimit)
{
    oci_runtime_spec_process_rlimits_element *rlimit = NULL;

    if (trans_ulimit_to_rlimit(&rlimit, ulimit) != 0) {
        return -1;
    }

    return do_merge_one_ulimit_override(oci_spec, rlimit);
}

static int merge_ulimits_override(oci_runtime_spec *oci_spec, host_config_ulimits_element **ulimits, size_t ulimits_len)
{
    int ret = 0;
    size_t i = 0;

    if (oci_spec == NULL || ulimits == NULL || ulimits_len == 0) {
        return -1;
    }

    ret = merge_ulimits_pre(oci_spec, ulimits_len);
    if (ret < 0) {
        goto out;
    }

    for (i = 0; i < ulimits_len; i++) {
        ret = merge_one_ulimit_override(oci_spec, ulimits[i]);
        if (ret != 0) {
            ret = -1;
            goto out;
        }
    }
out:
    return ret;
}

static int merge_conf_ulimits(oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = 0;

    /* rlimits */
    if (host_spec->ulimits != NULL && host_spec->ulimits_len != 0) {
        if (host_spec->ulimits_len > LIST_SIZE_MAX) {
            ERROR("Too many ulimits to add, the limit is %d", LIST_SIZE_MAX);
            lcrd_set_error_message("Too many ulimits to add, the limit is %d", LIST_SIZE_MAX);
            ret = -1;
            goto out;
        }
        ret = merge_ulimits_override(oci_spec, host_spec->ulimits, host_spec->ulimits_len);
        if (ret != 0) {
            ERROR("Failed to merge rlimits");
            goto out;
        }
    }

out:
    return ret;
}

static int merge_conf_hugetlbs(oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = 0;

    /* hugepage limits */
    if (host_spec->hugetlbs_len != 0 && host_spec->hugetlbs != NULL) {
        if (host_spec->hugetlbs_len > LIST_SIZE_MAX) {
            ERROR("Too many hugetlbs to add, the limit is %d", LIST_SIZE_MAX);
            lcrd_set_error_message("Too many hugetlbs to add, the limit is %d", LIST_SIZE_MAX);
            ret = -1;
            goto out;
        }
        ret = merge_hugetlbs(oci_spec, host_spec->hugetlbs, host_spec->hugetlbs_len);
        if (ret != 0) {
            ERROR("Failed to merge cgroup hugepage limits");
            goto out;
        }
    }

out:
    return ret;
}

static int merge_conf_pids_limit(oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = 0;

    /* pids limit */
    if (host_spec->pids_limit != 0) {
        ret = merge_pids_limit(oci_spec, host_spec->pids_limit);
        if (ret != 0) {
            ERROR("Failed to merge pids limit");
            goto out;
        }
    }

out:
    return ret;
}

static int merge_conf_cgroup(oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = 0;

    ret = merge_conf_cgroup_cpu(oci_spec, host_spec);
    if (ret != 0) {
        goto out;
    }

    ret = merge_conf_cgroup_memory(oci_spec, host_spec);
    if (ret != 0) {
        goto out;
    }

    ret = merge_conf_blkio_weight(oci_spec, host_spec);
    if (ret != 0) {
        goto out;
    }

    ret = merge_conf_ulimits(oci_spec, host_spec);
    if (ret != 0) {
        goto out;
    }

    ret = merge_conf_hugetlbs(oci_spec, host_spec);
    if (ret != 0) {
        goto out;
    }

    ret = merge_conf_pids_limit(oci_spec, host_spec);
    if (ret != 0) {
        goto out;
    }

out:
    return ret;
}

static int prepare_process_args(oci_runtime_spec *oci_spec, size_t args_len)
{
    int ret = 0;

    ret = make_sure_oci_spec_process(oci_spec);
    if (ret < 0) {
        return -1;
    }

    if (oci_spec->process->args_len != 0 && oci_spec->process->args != NULL) {
        size_t i;
        for (i = 0; i < oci_spec->process->args_len; i++) {
            free(oci_spec->process->args[i]);
            oci_spec->process->args[i] = NULL;
        }
        free(oci_spec->process->args);
        oci_spec->process->args = NULL;
        oci_spec->process->args_len = 0;
    }

    if (args_len > (SIZE_MAX / sizeof(char *))) {
        return -1;
    }

    oci_spec->process->args = util_common_calloc_s(args_len * sizeof(char *));
    if (oci_spec->process->args == NULL) {
        return -1;
    }
    return 0;
}

static int replace_entrypoint_cmds_from_spec(const oci_runtime_spec *oci_spec, container_custom_config *custom_spec)
{
    if (oci_spec->process->args_len == 0) {
        ERROR("No command specified");
        lcrd_set_error_message("No command specified");
        return -1;
    }
    return dup_array_of_strings((const char **)(oci_spec->process->args), oci_spec->process->args_len,
                                &(custom_spec->cmd), &(custom_spec->cmd_len));
}

static int merge_conf_args(oci_runtime_spec *oci_spec, container_custom_config *custom_spec)
{
    int ret = 0;
    size_t argslen = 0;
    size_t i = 0;

    // Reset entrypoint if we do not want to use entrypoint from image
    if (custom_spec->entrypoint_len == 1 && custom_spec->entrypoint[0][0] == '\0') {
        free(custom_spec->entrypoint[0]);
        custom_spec->entrypoint[0] = NULL;
        free(custom_spec->entrypoint);
        custom_spec->entrypoint = NULL;
        custom_spec->entrypoint_len = 0;
    }

    argslen = custom_spec->cmd_len;
    if (custom_spec->entrypoint_len != 0) {
        argslen += custom_spec->entrypoint_len;
    }

    if (argslen > LIST_SIZE_MAX) {
        ERROR("Too many commands to add, the limit is %d", LIST_SIZE_MAX);
        lcrd_set_error_message("Too many commands to add, the limit is %d", LIST_SIZE_MAX);
        return -1;
    }

    if (argslen == 0) {
        return replace_entrypoint_cmds_from_spec(oci_spec, custom_spec);
    }

    if (prepare_process_args(oci_spec, argslen) < 0) {
        ret = -1;
        goto out;
    }

    // append commands... to entrypoint
    for (i = 0; custom_spec->entrypoint != NULL && i < custom_spec->entrypoint_len; i++) {
        oci_spec->process->args[oci_spec->process->args_len] = util_strdup_s(custom_spec->entrypoint[i]);
        oci_spec->process->args_len++;
    }

    for (i = 0; custom_spec->cmd != NULL && i < custom_spec->cmd_len; i++) {
        oci_spec->process->args[oci_spec->process->args_len] = util_strdup_s(custom_spec->cmd[i]);
        oci_spec->process->args_len++;
    }

out:
    return ret;
}

static int merge_share_namespace_helper(const oci_runtime_spec *oci_spec, const char *mode, const char *type)
{
    int ret = -1;
    char *tmp_mode = NULL;
    size_t len = 0;
    size_t org_len = 0;
    size_t i = 0;
    oci_runtime_defs_linux_namespace_reference **work_ns = NULL;

    org_len = oci_spec->linux->namespaces_len;
    len = oci_spec->linux->namespaces_len;
    work_ns = oci_spec->linux->namespaces;

    tmp_mode = get_share_namespace_path(type, mode);
    for (i = 0; i < org_len; i++) {
        if (strcmp(mode, work_ns[i]->type) == 0) {
            free(work_ns[i]->path);
            work_ns[i]->path = NULL;
            if (tmp_mode != NULL) {
                work_ns[i]->path = util_strdup_s(tmp_mode);
            }
            break;
        }
    }
    if (i >= org_len) {
        if (len > (SIZE_MAX / sizeof(oci_runtime_defs_linux_namespace_reference *)) - 1) {
            ret = -1;
            ERROR("Out of memory");
            goto out;
        }

        ret = mem_realloc((void **)&work_ns, (len + 1) * sizeof(oci_runtime_defs_linux_namespace_reference *),
                          (void *)work_ns, len * sizeof(oci_runtime_defs_linux_namespace_reference *));
        if (ret != 0) {
            ERROR("Out of memory");
            goto out;
        }
        work_ns[len] = util_common_calloc_s(sizeof(oci_runtime_defs_linux_namespace_reference));
        if (work_ns[len] == NULL) {
            ERROR("Out of memory");
            ret = -1;
            goto out;
        }
        work_ns[len]->type = util_strdup_s(type);
        if (tmp_mode != NULL) {
            work_ns[len]->path = util_strdup_s(tmp_mode);
        }
        len++;
    }
    ret = 0;
out:
    free(tmp_mode);
    if (work_ns != NULL) {
        oci_spec->linux->namespaces = work_ns;
        oci_spec->linux->namespaces_len = len;
    }
    return ret;
}

static int merge_share_single_namespace(const oci_runtime_spec *oci_spec, const char *mode, const char *type)
{
    if (mode == NULL) {
        return 0;
    }

    return merge_share_namespace_helper(oci_spec, mode, type);
}

static int merge_share_namespace(oci_runtime_spec *oci_spec, const host_config *host_spec)
{
    int ret = -1;

    if (oci_spec == NULL || host_spec == NULL) {
        goto out;
    }

    if (make_sure_oci_spec_linux(oci_spec) < 0) {
        goto out;
    }

    // user
    if (merge_share_single_namespace(oci_spec, host_spec->userns_mode, TYPE_NAMESPACE_USER) != 0) {
        ret = -1;
        goto out;
    }

    // user remap
    if (host_spec->user_remap != NULL && merge_share_single_namespace(oci_spec, "user", TYPE_NAMESPACE_USER) != 0) {
        ret = -1;
        goto out;
    }

    // network
    if (merge_share_single_namespace(oci_spec, host_spec->network_mode, TYPE_NAMESPACE_NETWORK) != 0) {
        ret = -1;
        goto out;
    }

    // ipc
    if (merge_share_single_namespace(oci_spec, host_spec->ipc_mode, TYPE_NAMESPACE_IPC) != 0) {
        ret = -1;
        goto out;
    }

    // pid
    if (merge_share_single_namespace(oci_spec, host_spec->pid_mode, TYPE_NAMESPACE_PID) != 0) {
        ret = -1;
        goto out;
    }

    // uts
    if (merge_share_single_namespace(oci_spec, host_spec->uts_mode, TYPE_NAMESPACE_UTS) != 0) {
        ret = -1;
        goto out;
    }

    ret = 0;
out:
    return ret;
}

static int merge_working_dir(oci_runtime_spec *oci_spec, const char *working_dir)
{
    int ret = 0;

    if (working_dir == NULL) {
        return 0;
    }

    ret = make_sure_oci_spec_process(oci_spec);
    if (ret < 0) {
        goto out;
    }

    free(oci_spec->process->cwd);
    oci_spec->process->cwd = util_strdup_s(working_dir);

out:
    return ret;
}

static int change_tmpfs_mount_size(const oci_runtime_spec *oci_spec, int64_t memory_limit)
{
    int ret = 0;
    size_t i = 0;
    char size_opt[MOUNT_PROPERTIES_SIZE] = { 0 };

    if (oci_spec->mounts == NULL) {
        goto out;
    }
    if (memory_limit <= 0) {
        goto out;
    }
    /* set tmpfs mount size to half of container memory limit */
    int nret = snprintf(size_opt, sizeof(size_opt), "size=%lldk", (long long int)(memory_limit / 2048));
    if (nret < 0 || (size_t)nret >= sizeof(size_opt)) {
        ERROR("Out of memory");
        ret = -1;
        goto out;
    }
    for (i = 0; i < oci_spec->mounts_len; i++) {
        if (strcmp("tmpfs", oci_spec->mounts[i]->type) != 0) {
            continue;
        }
        if (strcmp("/run", oci_spec->mounts[i]->destination)  == 0 || \
            strcmp("/run/lock", oci_spec->mounts[i]->destination)  == 0 || \
            strcmp("/tmp", oci_spec->mounts[i]->destination) == 0) {
            ret = util_array_append(&oci_spec->mounts[i]->options, size_opt);
            if (ret != 0) {
                ERROR("append mount size option failed");
                goto out;
            }
            oci_spec->mounts[i]->options_len++;
        }
    }

out:
    return ret;
}

static int merge_settings_for_system_container(oci_runtime_spec *oci_spec, host_config *host_spec,
                                               container_custom_config *custom_spec)
{
    int ret = -1;

    if (oci_spec == NULL || host_spec == NULL) {
        return -1;
    }

    if (!host_spec->system_container) {
        return 0;
    }

    ret = adapt_settings_for_system_container(oci_spec, host_spec);
    if (ret != 0) {
        ERROR("Failed to adapt settings for system container");
        goto out;
    }
    if (change_tmpfs_mount_size(oci_spec, host_spec->memory) != 0) {
        ret = -1;
        ERROR("Failed to change tmpfs mount size for system container");
        goto out;
    }

    // append mounts of oci_spec
    if (custom_spec->ns_change_opt != NULL) {
        ret = adapt_settings_for_mounts(oci_spec, custom_spec);
        if (ret != 0) {
            ERROR("Failed to adapt settings for ns_change_opt");
            goto out;
        }
    }

out:
    return ret;
}

static int merge_resources_conf(oci_runtime_spec *oci_spec, host_config *host_spec,
                                container_custom_config *custom_spec, container_config_v2_common_config *common_config)
{
    int ret = 0;

    ret = merge_share_namespace(oci_spec, host_spec);
    if (ret != 0) {
        goto out;
    }

    ret = merge_conf_cgroup(oci_spec, host_spec);
    if (ret != 0) {
        goto out;
    }

    ret = merge_conf_device(oci_spec, host_spec);
    if (ret != 0) {
        goto out;
    }

    ret = merge_conf_mounts(oci_spec, custom_spec, host_spec, common_config);
    if (ret) {
        goto out;
    }
out:
    return ret;
}

static int merge_process_conf(oci_runtime_spec *oci_spec, const host_config *host_spec,
                              container_custom_config *custom_spec)
{
    int ret = 0;

    ret = merge_conf_args(oci_spec, custom_spec);
    if (ret != 0) {
        goto out;
    }

    /* environment variables */
    ret = merge_env(oci_spec, (const char **)custom_spec->env, custom_spec->env_len);
    if (ret != 0) {
        ERROR("Failed to merge environment variables");
        goto out;
    }

    /* env target file */
    ret = merge_env_target_file(oci_spec, host_spec->env_target_file);
    if (ret != 0) {
        ERROR("Failed to merge env target file");
        goto out;
    }

    /* working dir */
    ret = merge_working_dir(oci_spec, custom_spec->working_dir);
    if (ret != 0) {
        ERROR("Failed to merge working dir");
        goto out;
    }

    /* hook-spec file */
    ret = merge_hook_spec(oci_spec, host_spec->hook_spec);
    if (ret != 0) {
        ERROR("Failed to merge hook spec");
        goto out;
    }

out:
    return ret;
}

static int merge_security_conf(oci_runtime_spec *oci_spec, host_config *host_spec)
{
    int ret = 0;

    ret = merge_caps(oci_spec, (const char **)host_spec->cap_add, host_spec->cap_add_len,
                     (const char **)host_spec->cap_drop, host_spec->cap_drop_len);
    if (ret) {
        ERROR("Failed to merge caps");
        goto out;
    }

    ret = merge_default_seccomp_spec(oci_spec, oci_spec->process->capabilities);
    if (ret != 0) {
        ERROR("Failed to merge default seccomp file");
        goto out;
    }

    // merge external parameter
    ret = merge_seccomp(oci_spec, host_spec);
    if (ret != 0) {
        ERROR("Failed to merge user seccomp file");
        goto out;
    }

out:
    return ret;
}


static int merge_conf(oci_runtime_spec *oci_spec, host_config *host_spec,
                      container_custom_config *custom_spec, container_config_v2_common_config *common_config)
{
    int ret = 0;

    ret = merge_resources_conf(oci_spec, host_spec, custom_spec, common_config);
    if (ret != 0) {
        goto out;
    }

    ret = merge_process_conf(oci_spec, host_spec, custom_spec);
    if (ret != 0) {
        goto out;
    }

    // merge sysctl
    ret = merge_sysctls(oci_spec, host_spec->sysctls);
    if (ret != 0) {
        ret = -1;
        goto out;
    }

    ret = merge_security_conf(oci_spec, host_spec);
    if (ret != 0) {
        ERROR("Failed to merge user seccomp file");
        goto out;
    }

    /* settings for system container */
    ret = merge_settings_for_system_container(oci_spec, host_spec, custom_spec);
    if (ret != 0) {
        ERROR("Failed to merge system container conf");
        goto out;
    }

    /* settings for privileged */
    ret = adapt_settings_for_privileged(oci_spec, host_spec->privileged);
    if (ret != 0) {
        ERROR("Failed to adapt settings for privileged container");
        goto out;
    }

    ret = merge_hostname(oci_spec, host_spec, custom_spec);
    if (ret != 0) {
        ERROR("Failed to merge hostname");
        goto out;
    }

    ret = make_annotations(oci_spec, custom_spec, host_spec);
    if (ret != 0) {
        ret = -1;
        goto out;
    }

    ret = merge_no_new_privileges(oci_spec, host_spec);
    if (ret != 0) {
        ERROR("Failed to merge no new privileges");
        goto out;
    }

    ret = make_userns_remap(oci_spec, host_spec->user_remap);
    if (ret != 0) {
        ERROR("Failed to make user remap for container");
        goto out;
    }

out:
    return ret;
}

/* merge the default config with host config and image config and custom config */
oci_runtime_spec *merge_container_config(const char *id, const char *image_type, const char *image_name,
                                         const char *ext_config_image, host_config *host_spec,
                                         container_custom_config *custom_spec,
                                         container_config_v2_common_config *v2_spec, char **real_rootfs)
{
    oci_runtime_spec *oci_spec = NULL;
    parser_error err = NULL;
    int ret = 0;

    oci_spec = default_spec(host_spec->system_container);
    if (oci_spec == NULL) {
        goto out;
    }

    ret = make_sure_oci_spec_linux(oci_spec);
    if (ret < 0) {
        goto out;
    }

    ret = im_merge_image_config(id, image_type, image_name, ext_config_image, oci_spec, host_spec,
                                custom_spec, real_rootfs);
    if (ret != 0) {
        ERROR("Can not merge with image config");
        goto free_out;
    }

    if (*real_rootfs != NULL) {
        ret = merge_root(oci_spec, *real_rootfs, host_spec);
        if (ret != 0) {
            ERROR("Failed to merge root");
            goto free_out;
        }
        v2_spec->base_fs = util_strdup_s(*real_rootfs);
    }
    ret = merge_conf(oci_spec, host_spec, custom_spec, v2_spec);
    if (ret != 0) {
        ERROR("Failed to merge config");
        goto free_out;
    }

    goto out;

free_out:
    free_oci_runtime_spec(oci_spec);
    oci_spec = NULL;
out:
    free(err);
    return oci_spec;
}

static inline bool is_valid_umask_value(const char *value)
{
    return (strcmp(value, UMASK_NORMAL) == 0 || strcmp(value, UMASK_SECURE) == 0);
}

static int add_native_umask(const oci_runtime_spec *container)
{
    int ret = 0;
    size_t i = 0;
    char *umask = NULL;

    for (i = 0; i < container->annotations->len; i++) {
        if (strcmp(container->annotations->keys[i], ANNOTATION_UMAKE_KEY) == 0) {
            if (!is_valid_umask_value(container->annotations->values[i])) {
                ERROR("native.umask option %s not supported", container->annotations->values[i]);
                lcrd_set_error_message("native.umask option %s not supported", container->annotations->values[i]);
                ret = -1;
            }
            goto out;
        }
    }

    umask = conf_get_lcrd_native_umask();
    if (umask == NULL) {
        ERROR("Failed to get default native umask");
        ret = -1;
        goto out;
    }

    if (append_json_map_string_string(container->annotations, ANNOTATION_UMAKE_KEY, umask)) {
        ERROR("Failed to append annotations: native.umask=%s", umask);
        ret = -1;
        goto out;
    }

out:
    free(umask);
    return ret;
}

/* merge the default config with host config and custom config */
int merge_global_config(oci_runtime_spec *oci_spec)
{
    int ret = 0;

    ret = merge_global_hook(oci_spec);
    if (ret != 0) {
        ERROR("Failed to merge global hooks");
        goto out;
    }

    ret = merge_global_ulimit(oci_spec);
    if (ret != 0) {
        ERROR("Failed to merge global ulimit");
        goto out;
    }

    /* add rootfs.mount */
    ret = add_rootfs_mount(oci_spec);
    if (ret != 0) {
        ERROR("Failed to add rootfs mount");
        goto out;
    }

    /* add native.umask */
    ret = add_native_umask(oci_spec);
    if (ret != 0) {
        ERROR("Failed to add native umask");
        goto out;
    }

out:
    return ret;
}

/* read oci config */
oci_runtime_spec *read_oci_config(const char *rootpath, const char *name)
{
    int nret;
    char filename[PATH_MAX] = { 0x00 };
    parser_error err = NULL;
    oci_runtime_spec *ociconfig = NULL;

    nret = snprintf(filename, sizeof(filename), "%s/%s/%s", rootpath, name, OCICONFIGJSON);
    if (nret < 0 || (size_t)nret >= sizeof(filename)) {
        ERROR("Failed to print string");
        goto out;
    }

    ociconfig = oci_runtime_spec_parse_file(filename, NULL, &err);
    if (ociconfig == NULL) {
        ERROR("Failed to parse oci config file:%s", err);
        lcrd_set_error_message("Parse oci config file failed:%s", err);
        goto out;
    }
out:
    free(err);
    return ociconfig;
}

