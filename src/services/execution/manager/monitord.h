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
 * Author: maoweiyong
 * Create: 2017-11-22
 * Description: provide monitord definition
 ******************************************************************************/
#ifndef __LCRD_MONITORD_H
#define __LCRD_MONITORD_H
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#include "engine.h"

typedef enum {
    monitord_msg_state,
    monitord_msg_priority,
    monitord_msg_exit_code
} msg_type_t;

struct monitord_msg {
    msg_type_t type;
    char name[NAME_MAX + 1];
    int value;
    int exit_code;
    int pid;
};

struct monitord_sync_data {
    sem_t *monitord_sem;
    int *exit_code;
};

char *lcrd_monitor_fifo_name(const char *rootpath);

int connect_monitord(const char *rootpath);

int read_monitord_message_timeout(int fd, struct monitord_msg *msg, int timeout);

int new_monitord(struct monitord_sync_data *msync);

#endif
