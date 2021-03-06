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
 * Author: wujing
 * Create: 2020-12-15
 * Description: provide cri container manager service function definition
 *********************************************************************************/
#ifndef DAEMON_ENTRY_CRI_CONTAINER_MANAGER_H
#define DAEMON_ENTRY_CRI_CONTAINER_MANAGER_H
#include <memory>
#include <string>
#include <vector>

#include "api.pb.h"
#include "errors.h"

namespace CRI {
class ContainerManagerService {
public:
    ContainerManagerService() = default;
    virtual ~ContainerManagerService() = default;

    virtual auto CreateContainer(const std::string &podSandboxID,
                                 const runtime::v1alpha2::ContainerConfig &containerConfig,
                                 const runtime::v1alpha2::PodSandboxConfig &podSandboxConfig,
                                 Errors &error) -> std::string = 0;

    virtual void StartContainer(const std::string &containerID, Errors &error) = 0;

    virtual void StopContainer(const std::string &containerID, int64_t timeout, Errors &error) = 0;

    virtual void RemoveContainer(const std::string &containerID, Errors &error) = 0;

    virtual void ListContainers(const runtime::v1alpha2::ContainerFilter *filter,
                                std::vector<std::unique_ptr<runtime::v1alpha2::Container>> *containers,
                                Errors &error) = 0;

    virtual void ListContainerStats(const runtime::v1alpha2::ContainerStatsFilter *filter,
                                    std::vector<std::unique_ptr<runtime::v1alpha2::ContainerStats>> *containerstats,
                                    Errors &error) = 0;

    virtual auto ContainerStatus(const std::string &containerID,
                                 Errors &error) -> std::unique_ptr<runtime::v1alpha2::ContainerStatus> = 0;

    virtual void UpdateContainerResources(const std::string &containerID,
                                          const runtime::v1alpha2::LinuxContainerResources &resources,
                                          Errors &error) = 0;

    virtual void ExecSync(const std::string &containerID, const google::protobuf::RepeatedPtrField<std::string> &cmd,
                          int64_t timeout, runtime::v1alpha2::ExecSyncResponse *reply, Errors &error) = 0;

    virtual void Exec(const runtime::v1alpha2::ExecRequest &req, runtime::v1alpha2::ExecResponse *resp,
                      Errors &error) = 0;

    virtual void Attach(const runtime::v1alpha2::AttachRequest &req, runtime::v1alpha2::AttachResponse *resp,
                        Errors &error) = 0;
};
} // namespace CRI

#endif // DAEMON_ENTRY_CRI_CONTAINER_MANAGER_H