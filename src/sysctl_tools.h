/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2018-2019. All rights reserved.
 * iSulad licensed under the Mulan PSL v1.
 * You can use this software according to the terms and conditions of the Mulan PSL v1.
 * You may obtain a copy of Mulan PSL v1 at:
 *     http://license.coscl.org.cn/MulanPSL
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v1 for more details.
 * Author: tanyifeng
 * Create: 2018-11-1
 * Description: provide sysctl function definition
 ********************************************************************************/
#ifndef _ISULA_SYSCTL_TOOLS_
#define _ISULA_SYSCTL_TOOLS_

#ifdef __cplusplus
extern "C" {
#endif

#define SYSCTL_BASE "/proc/sys"

int get_sysctl(const char *sysctl, char **err);

int set_sysctl(const char *sysctl, int new_value, char **err);

#ifdef __cplusplus
}
#endif
#endif
