/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2019. All rights reserved.
 * iSulad licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan PSL v1.
 * You may obtain a copy of Mulan PSL v1 at:
 *     http://license.coscl.org.cn/MulanPSL
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v1 for more details.
 * Author: lifeng
 * Create: 2019-11-22
 * Description: provide container list callback function definition
 ********************************************************************************/

#include <stdio.h>
#include <unistd.h>
#include <limits.h>

#include "lcr_rt_ops.h"
#include "log.h"
#include "engine.h"
#include "callback.h"
#include "error.h"
#include "lcrd_config.h"
#include "runtime.h"

bool rt_lcr_detect(const char *runtime)
{
    /* now we just support lcr engine */
    if (runtime != NULL && strcasecmp(runtime, "lcr") == 0) {
        return true;
    }

    return false;
}

int rt_lcr_create(const char *name, const char *runtime, const rt_create_params_t *params)
{
    int ret = 0;
    char *runtime_root = NULL;
    struct engine_operation *engine_ops = NULL;

    runtime_root = conf_get_routine_rootdir(runtime);
    if (runtime_root == NULL) {
        ERROR("Root path is NULL");
        ret = -1;
        goto out;
    }

    engine_ops = engines_get_handler(runtime);
    if (engine_ops == NULL || engine_ops->engine_create_op == NULL) {
        ERROR("Failed to get engine create operations");
        ret = -1;
        goto out;
    }

    if (!engine_ops->engine_create_op(name, runtime_root, params->real_rootfs, params->oci_config_data)) {
        ERROR("Failed to create container");
        const char *tmpmsg = NULL;
        tmpmsg = engine_ops->engine_get_errmsg_op();
        lcrd_set_error_message("Create container error: %s",
                               (tmpmsg && strcmp(tmpmsg, DEF_SUCCESS_STR)) ? tmpmsg
                               : DEF_ERR_RUNTIME_STR);
        engine_ops->engine_clear_errmsg_op();
        ret = -1;
        goto out;
    }

out:
    free(runtime_root);
    return ret;
}

static int parse_container_pid(const char *S, container_pid_t *pid_info)
{
    int num;

    num = sscanf(S, "%d %Lu %d %Lu", &pid_info->pid, &pid_info->start_time, &pid_info->ppid, &pid_info->pstart_time);
    if (num != 4) { // args num to read is 4
        ERROR("Call sscanf error: %s", errno ? strerror(errno) : "");
        return -1;
    }

    return 0;
}

static int lcr_rt_read_pidfile(const char *pidfile, container_pid_t *pid_info)
{
    if (pidfile == NULL || pid_info == NULL) {
        ERROR("Invalid input arguments");
        return -1;
    }

    char sbuf[1024] = { 0 };  /* bufs for stat */

    if ((util_file2str(pidfile, sbuf, sizeof(sbuf))) == -1) {
        return -1;
    }

    return parse_container_pid(sbuf, pid_info);
}

int rt_lcr_start(const char *name, const char *runtime, const rt_start_params_t *params, container_pid_t *pid_info)
{
    int ret = 0;
    struct engine_operation *engine_ops = NULL;
    engine_start_request_t request = { 0 };

    engine_ops = engines_get_handler(runtime);
    if (engine_ops == NULL || engine_ops->engine_start_op == NULL) {
        ERROR("Failed to get engine start operations");
        ret = -1;
        goto out;
    }

    request.name = name;
    request.lcrpath = params->rootpath;
    request.logpath = params->logpath;
    request.loglevel = params->loglevel;
    request.daemonize = true;
    request.tty = params->tty;
    request.open_stdin = params->open_stdin;
    request.console_fifos = params->console_fifos;
    request.start_timeout = params->start_timeout;
    request.container_pidfile = params->container_pidfile;
    request.exit_fifo = params->exit_fifo;
    request.uid = params->puser->uid;
    request.gid = params->puser->gid;
    request.additional_gids = params->puser->additional_gids;
    request.additional_gids_len = params->puser->additional_gids_len;

    if (!engine_ops->engine_start_op(&request)) {
        const char *tmpmsg = NULL;
        tmpmsg = engine_ops->engine_get_errmsg_op();
        lcrd_set_error_message("Start container error: %s",
                               (tmpmsg && strcmp(tmpmsg, DEF_SUCCESS_STR)) ? tmpmsg
                               : DEF_ERR_RUNTIME_STR);
        ERROR("Start container error: %s", (tmpmsg && strcmp(tmpmsg, DEF_SUCCESS_STR)) ? tmpmsg
              : DEF_ERR_RUNTIME_STR);
        engine_ops->engine_clear_errmsg_op();
        ret = -1;
        goto out;
    }
    ret = lcr_rt_read_pidfile(params->container_pidfile, pid_info);
    if (ret != 0) {
        ERROR("Failed to get started container's pid info, start container fail");
        ret = -1;
        goto out;
    }
out:
    return ret;
}

int rt_lcr_restart(const char *name, const char *runtime, const rt_restart_params_t *params)
{
    return RUNTIME_NOT_IMPLEMENT_RESET;
}

int rt_lcr_clean_resource(const char *name, const char *runtime, const rt_clean_params_t *params)
{
    int ret = 0;
    struct engine_operation *engine_ops = NULL;

    engine_ops = engines_get_handler(runtime);
    if (engine_ops == NULL || engine_ops->engine_clean_op == NULL) {
        ERROR("Failed to get engine clean operations");
        ret = -1;
        goto out;
    }

    if (!engine_ops->engine_clean_op(name, params->rootpath, params->logpath, params->loglevel, params->pid)) {
        const char *tmpmsg = NULL;
        tmpmsg = engine_ops->engine_get_errmsg_op();
        lcrd_try_set_error_message("Clean resource container error;%s",
                                   (tmpmsg && strcmp(tmpmsg, DEF_SUCCESS_STR)) ? tmpmsg
                                   : DEF_ERR_RUNTIME_STR);
        ret = -1;
        goto out;
    }

out:
    if (engine_ops != NULL) {
        engine_ops->engine_clear_errmsg_op();
    }
    return ret;
}

static int remove_container_rootpath(const char *id, const char *root_path)
{
    int ret = 0;
    char cont_root_path[PATH_MAX] = { 0 };

    ret = snprintf(cont_root_path, sizeof(cont_root_path), "%s/%s", root_path, id);
    if (ret < 0 || (size_t)ret >= sizeof(cont_root_path)) {
        ERROR("Failed to sprintf container_state");
        ret = -1;
        goto out;
    }
    ret = util_recursive_rmdir(cont_root_path, 0);
    if (ret != 0) {
        const char *tmp_err = (errno != 0) ? strerror(errno) : "error";
        ERROR("Failed to delete container's root directory %s: %s", cont_root_path, tmp_err);
        lcrd_set_error_message("Failed to delete container's root directory %s: %s", cont_root_path, tmp_err);
        ret = -1;
        goto out;
    }

out:
    return ret;
}

int rt_lcr_rm(const char *name, const char *runtime, const rt_rm_params_t *params)
{
    int ret = 0;
    struct engine_operation *engine_ops = NULL;

    engine_ops = engines_get_handler(runtime);
    if (engine_ops == NULL || engine_ops->engine_delete_op == NULL) {
        ERROR("Failed to get engine delete operations");
        ret = -1;
        goto out;
    }

    if (!engine_ops->engine_delete_op(name, params->rootpath)) {
        ret = -1;
        const char *tmpmsg = engine_ops->engine_get_errmsg_op();
        lcrd_set_error_message("Runtime delete container error: %s",
                               (tmpmsg != NULL && strcmp(tmpmsg, DEF_SUCCESS_STR)) ? tmpmsg : DEF_ERR_RUNTIME_STR);
        ERROR("Runtime delete container error: %s",
              (tmpmsg != NULL && strcmp(tmpmsg, DEF_SUCCESS_STR)) ? tmpmsg : DEF_ERR_RUNTIME_STR);
        if (tmpmsg != NULL && strstr(tmpmsg, "No such container") != NULL) {
            // container root path may been corrupted, try to remove by daemon
            WARN("container %s root path may been corrupted, try to remove by daemon", name);
            if (remove_container_rootpath(name, params->rootpath) == 0) {
                ret = 0;
                goto out;
            }
        }
        goto out;
    }

out:
    if (engine_ops != NULL) {
        engine_ops->engine_clear_errmsg_op();
    }
    return ret;
}

int rt_lcr_get_console_config(const char *name, const char *runtime, const rt_get_console_conf_params_t *params)
{
    int ret = 0;
    struct engine_operation *engine_ops = NULL;

    engine_ops = engines_get_handler(runtime);
    if (engine_ops == NULL || (engine_ops->engine_get_console_config_op) == NULL) {
        ERROR("Failed to get engine get_console_config operation");
        ret = -1;
        goto out;
    }

    if (!engine_ops->engine_get_console_config_op(name, params->rootpath, params->config)) {
        ERROR("Failed to get console config");
        const char *tmpmsg = NULL;
        tmpmsg = engine_ops->engine_get_errmsg_op();
        lcrd_set_error_message("Get console config error;%s", (tmpmsg && strcmp(tmpmsg, DEF_SUCCESS_STR)) ?
                               tmpmsg : DEF_ERR_RUNTIME_STR);
        ret = -1;
        goto out;
    }


out:
    if (engine_ops != NULL) {
        engine_ops->engine_clear_errmsg_op();
    }
    return ret;
}

int rt_lcr_status(const char *name, const char *runtime, const rt_status_params_t *params,
                  struct engine_container_info *status)
{
    int ret = 0;
    int nret = 0;
    struct engine_operation *engine_ops = NULL;

    engine_ops = engines_get_handler(runtime);
    if (engine_ops == NULL || engine_ops->engine_get_container_status_op == NULL) {
        ERROR("Failed to get engine status operations");
        ret = -1;
        goto out;
    }

    nret = engine_ops->engine_get_container_status_op(name, params->rootpath, status);
    if (nret != 0) {
        engine_ops->engine_clear_errmsg_op();
        ret = -1;
        goto out;
    }

out:
    return ret;
}

int rt_lcr_exec(const char *id, const char *runtime, const rt_exec_params_t *params, int *exit_code)
{
    int ret = 0;
    struct engine_operation *engine_ops = NULL;
    engine_exec_request_t request = { 0 };

    engine_ops = engines_get_handler(runtime);
    if (engine_ops == NULL || engine_ops->engine_exec_op == NULL) {
        DEBUG("Failed to get engine exec operations");
        ret = -1;
        goto out;
    }

    request.name = id;
    request.lcrpath = params->rootpath;
    request.logpath = params->logpath;
    request.loglevel = params->loglevel;
    request.args = (const char **)params->args;
    request.args_len = params->args_len;
    request.env = (const char **)params->envs;
    request.env_len = params->envs_len;
    request.console_fifos = params->console_fifos;
    request.timeout = params->timeout;
    request.user = params->user;

    if (!engine_ops->engine_exec_op(&request, exit_code)) {
        const char *tmpmsg = NULL;
        tmpmsg = engine_ops->engine_get_errmsg_op();
        lcrd_set_error_message("Exec container error;%s", (tmpmsg && strcmp(tmpmsg, DEF_SUCCESS_STR)) ?
                               tmpmsg : DEF_ERR_RUNTIME_STR);
        util_contain_errmsg(g_lcrd_errmsg, exit_code);
        engine_ops->engine_clear_errmsg_op();
        ret = -1;
        goto out;
    }

out:
    return ret;
}

int rt_lcr_pause(const char *name, const char *runtime, const rt_pause_params_t *params)
{
    int ret = 0;
    struct engine_operation *engine_ops = NULL;

    engine_ops = engines_get_handler(runtime);
    if (engine_ops == NULL || engine_ops->engine_pause_op == NULL) {
        DEBUG("Failed to get engine pause operations");
        ret = -1;
        goto out;
    }

    if (!engine_ops->engine_pause_op(name, params->rootpath)) {
        DEBUG("Pause container %s failed", name);
        const char *tmpmsg = NULL;
        tmpmsg = engine_ops->engine_get_errmsg_op();
        lcrd_set_error_message("Pause container error;%s", (tmpmsg && strcmp(tmpmsg, DEF_SUCCESS_STR)) ?
                               tmpmsg : DEF_ERR_RUNTIME_STR);
        engine_ops->engine_clear_errmsg_op();
        ret = -1;
        goto out;
    }
out:
    return ret;
}

int rt_lcr_resume(const char *name, const char *runtime, const rt_resume_params_t *params)
{
    int ret = 0;
    struct engine_operation *engine_ops = NULL;

    engine_ops = engines_get_handler(runtime);
    if (engine_ops == NULL || engine_ops->engine_resume_op == NULL) {
        DEBUG("Failed to get engine resume operations");
        ret = -1;
        goto out;
    }

    if (!engine_ops->engine_resume_op(name, params->rootpath)) {
        DEBUG("Resume container %s failed", name);
        const char *tmpmsg = NULL;
        tmpmsg = engine_ops->engine_get_errmsg_op();
        lcrd_set_error_message("Resume container error;%s",
                               (tmpmsg && strcmp(tmpmsg, DEF_SUCCESS_STR)) ? tmpmsg
                               : DEF_ERR_RUNTIME_STR);

        engine_ops->engine_clear_errmsg_op();
        ret = -1;
        goto out;
    }
out:
    return ret;
}
