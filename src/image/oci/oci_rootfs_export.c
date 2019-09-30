/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2019-2019. All rights reserved.
 * iSulad licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan PSL v1.
 * You may obtain a copy of Mulan PSL v1 at:
 *     http://license.coscl.org.cn/MulanPSL
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v1 for more details.
 * Author: wangfengtu
 * Create: 2019-04-06
 * Description: provide oci export rootfs functions
 ******************************************************************************/

#define _GNU_SOURCE             /* See feature_test_macros(7) */
#include <fcntl.h>              /* Obtain O_* constant definitions */
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "securec.h"
#include "oci_rootfs_export.h"
#include "utils.h"
#include "log.h"
#include "liblcrd.h"
#include "isula_imtool_interface.h"

static bool do_export(rootfs_export_request *request)
{
    bool ret = false;
    bool command_ret = false;
    char *stdout_buffer = NULL;
    char *stderr_buffer = NULL;

    command_ret = util_exec_cmd(execute_export_rootfs, request, NULL, &stdout_buffer,
                                &stderr_buffer);
    if (!command_ret) {
        if (stderr_buffer != NULL) {
            ERROR("Failed to export rootfs with error: %s", stderr_buffer);
            lcrd_set_error_message("Failed to export rootfs with error: %s",
                                   stderr_buffer);
        } else {
            ERROR("Failed to exec export rootfs command");
            lcrd_set_error_message("Failed to exec export rootfs command");
        }
        goto free_out;
    }

    ret = true;

free_out:
    free(stderr_buffer);
    free(stdout_buffer);
    return ret;
}

static int check_export_request_valid(rootfs_export_request *request)
{
    int ret = -1;

    if (request == NULL) {
        ERROR("unvalid export request");
        lcrd_set_error_message("unvalid export request");
        goto out;
    }

    if (request->id == NULL) {
        ERROR("Export rootfs requires container name");
        lcrd_set_error_message("Export rootfs requires container name");
        goto out;
    }

    if (request->file == NULL) {
        ERROR("Export rootfs requires output file path");
        lcrd_set_error_message("Export rootfs requires output file path");
        goto out;
    }

    ret = 0;

out:
    return ret;
}


int export_rootfs(rootfs_export_request *request,
                  rootfs_export_response **response)
{
    int ret = 0;

    if (response == NULL) {
        ERROR("Invalid input arguments");
        return -1;
    }

    *response = util_common_calloc_s(sizeof(rootfs_export_response));
    if (*response == NULL) {
        ERROR("Out of memory");
        return -1;
    }

    if (check_export_request_valid(request) != 0) {
        ret = -1;
        goto pack_response;
    }

    if (!do_export(request)) {
        ERROR("Failed to export rootfs");
        ret = -1;
        goto pack_response;
    }

pack_response:
    if (g_lcrd_errmsg != NULL) {
        (*response)->errmsg = util_strdup_s(g_lcrd_errmsg);
    }

    return ret;
}

void free_rootfs_export_request(rootfs_export_request *ptr)
{
    if (ptr == NULL) {
        return;
    }
    free(ptr->id);
    ptr->id = NULL;
    free(ptr->file);
    ptr->file = NULL;

    free(ptr);
}

void free_rootfs_export_response(rootfs_export_response *ptr)
{
    if (ptr == NULL) {
        return;
    }

    free(ptr->errmsg);
    ptr->errmsg = NULL;

    free(ptr);
}

