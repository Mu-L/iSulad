/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2018-2021. All rights reserved.
 * iSulad licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: wujing
 * Create: 2018-11-08
 * Description: provide websocket stream service definition
 ******************************************************************************/
#ifndef DAEMON_ENTRY_CRI_STREAMS_SERVICE_STREAM_SERVER_H
#define DAEMON_ENTRY_CRI_STREAMS_SERVICE_STREAM_SERVER_H
#include "errors.h"
#include "url.h"

url::URLDatum cri_stream_server_url(void);

#ifdef __cplusplus
extern "C" {
#endif

void cri_stream_server_init(Errors &err);

void cri_stream_server_wait(void);

void cri_stream_server_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // DAEMON_ENTRY_CRI_STREAMS_SERVICE_STREAM_SERVER_H


