/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2020. All rights reserved.
 * iSulad licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: lifeng
 * Create: 2020-03-14
 * Description: provide tar function definition
 *********************************************************************************/
#ifndef UTILS_TAR_UTIL_ARCHIVE_H
#define UTILS_TAR_UTIL_ARCHIVE_H

#include <stdbool.h>
#include <stdint.h>

#include "io_wrapper.h"

#define ARCHIVE_BLOCK_SIZE (32 * 1024)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NONE_WHITEOUT_FORMATE = 0, // handle whiteouts as normal files
    OVERLAY_WHITEOUT_FORMATE = 1, // handle whiteouts as the way as overlay
    REMOVE_WHITEOUT_FORMATE = 2, // handle whiteouts by removing the target files
} whiteout_format_type;

struct archive_options {
    whiteout_format_type whiteout_format;

#ifdef ENABLE_USERNS_REMAP
    uid_t uid;
    gid_t gid;
#endif
    // rename archive entry's name from src_base to dst_base
    const char *src_base;
    const char *dst_base;
};

int archive_unpack(const struct io_read_wrapper *content, const char *dstdir, const struct archive_options *options,
                   const char *root_dir, char **errmsg);

bool valid_archive_format(const char *file);

int archive_chroot_tar(const char *path, const char *file, const char *root_dir, char **errmsg);

int archive_chroot_tar_stream(const char *chroot_dir, const char *tar_path, const char *src_base,
                              const char *dst_base, const char *root_dir, struct io_read_wrapper *content);
int archive_chroot_untar_stream(const struct io_read_wrapper *context, const char *chroot_dir, const char *untar_dir,
                                const char *src_base, const char *dst_base, const char *root_dir, char **errmsg);

int archive_copy_oci_tar_split_and_ret_size(int src_fd, const char *dist_file, int64_t *ret_size);

#ifdef __cplusplus
}
#endif

#endif
