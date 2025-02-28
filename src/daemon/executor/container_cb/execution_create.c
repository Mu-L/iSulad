/******************************************************************************
 * Copyright (c) Huawei Technologies Co., Ltd. 2017-2019. All rights reserved.
 * iSulad licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *     http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
 * PURPOSE.
 * See the Mulan PSL v2 for more details.
 * Author: tanyifeng
 * Create: 2017-11-22
 * Description: provide container create callback function definition
 ********************************************************************************/
#include "execution_create.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <malloc.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <isula_libutils/log.h>
#include <isula_libutils/auto_cleanup.h>
#include <isula_libutils/container_config.h>
#include <isula_libutils/container_config_v2.h>
#include <isula_libutils/defs.h>
#include <isula_libutils/host_config.h>
#include <isula_libutils/isulad_daemon_configs.h>
#include <isula_libutils/json_common.h>
#include <isula_libutils/oci_runtime_spec.h>

#include "isulad_config.h"
#include "specs_api.h"
#include "verify.h"
#include "container_api.h"
#include "execution_network.h"
#include "plugin_api.h"
#include "image_api.h"
#include "utils.h"
#include "error.h"
#include "constants.h"
#include "events_sender_api.h"
#include "sysinfo.h"
#include "service_container_api.h"
#include "daemon_arguments.h"
#include "err_msg.h"
#include "event_type.h"
#include "utils_file.h"
#include "utils_string.h"
#include "utils_timestamp.h"
#include "utils_network.h"
#include "utils_verify.h"
#include "network_namespace.h"
#include "namespace.h"
#include "selinux_label.h"
#include "opt_log.h"
#include "runtime_api.h"
#include "id_name_manager.h"
#include "mailbox.h"
#include "specs_mount.h"

#ifdef ENABLE_NRI
#include "nri_spec.h"
#endif

#ifdef ENABLE_CRI_API_V1
static bool validate_sandbox_info(const container_sandbox_info *sandbox)
{
    bool valid = (sandbox->sandboxer != NULL && sandbox->id != NULL &&
                  sandbox->hostname != NULL && sandbox->hostname_path != NULL &&
                  sandbox->hosts_path != NULL && sandbox->resolv_conf_path != NULL);
    return valid;
}
#endif

static int create_request_check(const container_create_request *request)
{
    int ret = 0;
    parser_error err = NULL;

    if (request == NULL) {
        ERROR("Receive NULL container id");
        ret = -1;
        goto out;
    }

    if ((request->rootfs == NULL && request->image == NULL)) {
        ERROR("Container image or rootfs error");
        ret = -1;
        goto out;
    }

    if (request->image != NULL && !util_valid_image_name(request->image)) {
        ERROR("invalid image name %s", request->image);
        isulad_set_error_message("Invalid image name '%s'", request->image);
        ret = -1;
        goto out;
    }

#ifdef ENABLE_CRI_API_V1
    if (request->sandbox != NULL && !validate_sandbox_info(request->sandbox)) {
        ERROR("Invalid sandbox info");
        ret = -1;
        goto out;
    }
#endif

    if (request->hostconfig == NULL) {
        ERROR("Receive NULL Request hostconfig");
        ret = -1;
        goto out;
    }

    if (request->customconfig == NULL) {
        ERROR("Receive NULL Request customconfig");
        ret = -1;
        goto out;
    }

out:
    free(err);
    return ret;
}

static host_config *get_host_spec_from_request(const container_create_request *request)
{
    parser_error err = NULL;
    host_config *host_spec = NULL;

    host_spec = host_config_parse_data(request->hostconfig, NULL, &err);
    if (host_spec == NULL) {
        ERROR("Failed to parse host config data:%s", err);
    }
    free(err);
    return host_spec;
}

static int merge_external_rootfs_to_host_config(host_config *host_spec, const char *external_rootfs)
{
    if (host_spec == NULL) {
        return -1;
    }
    host_spec->external_rootfs = external_rootfs != NULL ? util_strdup_s(external_rootfs) : NULL;

    return 0;
}

static host_config *get_host_spec(const container_create_request *request, const sysinfo_t *sysinfo)
{
    host_config *host_spec = NULL;

    host_spec = get_host_spec_from_request(request);
    if (host_spec == NULL) {
        return NULL;
    }

    if (merge_external_rootfs_to_host_config(host_spec, request->rootfs) != 0) {
        goto error_out;
    }

    if (verify_host_config_settings(host_spec, sysinfo, false)) {
        ERROR("Failed to verify host config settings");
        goto error_out;
    }

    return host_spec;

error_out:
    free_host_config(host_spec);
    return NULL;
}

static container_config *get_container_spec_from_request(const container_create_request *request)
{
    parser_error err = NULL;

    container_config *container_spec = container_config_parse_data(request->customconfig, NULL, &err);
    if (container_spec == NULL) {
        ERROR("Failed to parse custom config data:%s", err);
    }

    free(err);
    return container_spec;
}

static void set_container_log_config_driver(isulad_daemon_configs_container_log *opts, container_config *container_spec)
{
    if (container_spec->log_driver != NULL) {
        return;
    }

    // use daemon container log driver
    container_spec->log_driver = util_strdup_s(opts->driver);
    if (container_spec->log_driver != NULL) {
        return;
    }

    // use default container log driver
    container_spec->log_driver = util_strdup_s(CONTAINER_LOG_CONFIG_JSON_FILE_DRIVER);
}

static int merge_container_log_config_opts(const char *daemon_driver, const json_map_string_string *daemon_opts,
                                           container_config *spec)
{
    size_t i, j;

    if (daemon_driver == NULL || strcmp(daemon_driver, spec->log_driver) != 0) {
        // daemon driver different with spec, just ignore log opts of daemon
        return 0;
    }

    // merge daemon container log opts into spec
    for (i = 0; daemon_opts != NULL && i < daemon_opts->len; i++) {
        for (j = 0; j < spec->annotations->len; j++) {
            if (strcmp(spec->annotations->keys[j], daemon_opts->keys[i]) == 0) {
                break;
            }
        }
        if (j == spec->annotations->len &&
            append_json_map_string_string(spec->annotations, daemon_opts->keys[i], daemon_opts->values[i]) != 0) {
            ERROR("Out of memory");
            return -1;
        }
    }

    return 0;
}

static int do_set_default_log_path_for_json_file(const char *id, const char *root, container_config *spec)
{
    int nret = 0;
    char default_path[PATH_MAX] = { 0 };

    nret = snprintf(default_path, PATH_MAX, "%s/%s/console.log", root, id);
    if (nret < 0 || (size_t)nret >= PATH_MAX) {
        ERROR("Create default log path for container %s failed", id);
        return -1;
    }
    nret = append_json_map_string_string(spec->annotations, CONTAINER_LOG_CONFIG_KEY_FILE, default_path);
    if (nret != 0) {
        ERROR("Out of memory");
        return -1;
    }

    return 0;
}

int syslog_tag_parser(const char *tag, map_t *tag_maps, char **parsed)
{
    char *tmp_tag = NULL;
    int ret = 0;
    char *target = NULL;

    if (tag == NULL) {
        ERROR("empty tag is invalid.");
        return -1;
    }

    tmp_tag = util_strdup_s(tag);
    tmp_tag = util_trim_space(tmp_tag);
    target = map_search(tag_maps, (void *)tmp_tag);
    if (target == NULL) {
        ERROR("Invalid tag: %s", tag);
        ret = -1;
        goto out;
    }

    *parsed = util_strdup_s(target);

out:
    free(tmp_tag);
    return ret;
}

static int do_update_container_log_config_syslog_tag(map_t *tag_maps, const char *driver, size_t idx,
                                                     json_map_string_string *annotations)
{
    char *parsed_tag = NULL;

    if (driver == NULL || strcmp(driver, CONTAINER_LOG_CONFIG_SYSLOG_DRIVER) != 0) {
        return 0;
    }

    if (annotations->keys[idx] == NULL || strcmp(annotations->keys[idx], CONTAINER_LOG_CONFIG_KEY_SYSLOG_TAG) != 0) {
        return 0;
    }

    if (parse_container_log_opt_syslog_tag(annotations->values[idx], syslog_tag_parser, tag_maps, &parsed_tag) != 0) {
        return -1;
    }
    DEBUG("new syslog tag: %s", parsed_tag);

    free(annotations->values[idx]);
    annotations->values[idx] = parsed_tag;
    return 0;
}

static map_t *make_tag_mappings(const struct logger_info *p_info)
{
#define SHORT_ID_LEN 12
    map_t *tag_maps = NULL;
    char *short_id = NULL;
    char *short_img_id = NULL;

    tag_maps = map_new(MAP_STR_STR, MAP_DEFAULT_CMP_FUNC, MAP_DEFAULT_FREE_FUNC);
    if (tag_maps == NULL) {
        ERROR("Out of memory");
        return NULL;
    }

    short_id = util_sub_string(p_info->id, 0, SHORT_ID_LEN);
    if (short_id == NULL) {
        goto err_out;
    }
    if (!map_replace(tag_maps, (void *)".ID", (void *)short_id)) {
        goto err_out;
    }
    if (!map_replace(tag_maps, (void *)".FullID", (void *)p_info->id)) {
        goto err_out;
    }
    if (!map_replace(tag_maps, (void *)".Name", (void *)p_info->name)) {
        goto err_out;
    }

    if (p_info->img_id != NULL) {
        short_img_id = util_sub_string(p_info->img_id, 0, SHORT_ID_LEN);
        if (short_img_id == NULL) {
            goto err_out;
        }
        if (!map_replace(tag_maps, (void *)".ImageID", (void *)short_img_id)) {
            goto err_out;
        }
        if (!map_replace(tag_maps, (void *)".ImageFullID", (void *)p_info->img_id)) {
            goto err_out;
        }
    } else {
        WARN("Empty image id");
    }

    if (p_info->img_name != NULL) {
        if (!map_replace(tag_maps, (void *)".ImageName", (void *)p_info->img_name)) {
            goto err_out;
        }
    } else {
        WARN("Empty image name");
    }

    if (!map_replace(tag_maps, (void *)".DaemonName", (void *)p_info->daemon_name)) {
        goto err_out;
    }

    free(short_img_id);
    free(short_id);
    return tag_maps;
err_out:
    free(short_img_id);
    free(short_id);
    map_free(tag_maps);
    return NULL;
}

static int do_set_default_container_log_opts(bool set_file, bool set_rotate, bool set_size, const char *id,
                                             const char *root, container_config *spec)
{
    if (!set_rotate && append_json_map_string_string(spec->annotations, CONTAINER_LOG_CONFIG_KEY_ROTATE, "7") != 0) {
        return -1;
    }
    if (!set_size && append_json_map_string_string(spec->annotations, CONTAINER_LOG_CONFIG_KEY_SIZE, "1MB") != 0) {
        return -1;
    }
    if (set_file) {
        return 0;
    }
    return do_set_default_log_path_for_json_file(id, root, spec);
}

static int do_parse_container_log_config_opts(const struct logger_info *p_info, const char *root,
                                              container_config *spec)
{
    size_t i;
    bool set_file = false;
    bool set_rotate = false;
    bool set_size = false;
    map_t *tag_maps = NULL;
    int ret = 0;

    tag_maps = make_tag_mappings(p_info);
    if (tag_maps == NULL) {
        ERROR("Out of memory");
        return -1;
    }

    // check log opts is support by driver
    for (i = 0; i < spec->annotations->len; i++) {
        const char *tmp_key = spec->annotations->keys[i];
        if (strncmp(tmp_key, CONTAINER_LOG_CONFIG_KEY_PREFIX, strlen(CONTAINER_LOG_CONFIG_KEY_PREFIX)) != 0) {
            // ignore other configs
            continue;
        }
        DEBUG("check log opt key: %s for driver: %s", tmp_key, spec->log_driver);
        if (!check_opt_container_log_opt(spec->log_driver, tmp_key)) {
            isulad_set_error_message("container log driver: %s, unsupport: %s", spec->log_driver, tmp_key);
            ERROR("container log driver: %s, unsupport: %s", spec->log_driver, tmp_key);
            ret = -1;
            goto out;
        }

        if (do_update_container_log_config_syslog_tag(tag_maps, spec->log_driver, i, spec->annotations) != 0) {
            isulad_set_error_message("container syslog tag: unsupport: %s", spec->annotations->values[i]);
            ERROR("container syslog tag: unsupport: %s", spec->annotations->values[i]);
            ret = -1;
            goto out;
        }

        if (strcmp(CONTAINER_LOG_CONFIG_KEY_FILE, tmp_key) == 0) {
            set_file = true;
        }
        if (strcmp(CONTAINER_LOG_CONFIG_KEY_ROTATE, tmp_key) == 0) {
            set_rotate = true;
        }
        if (strcmp(CONTAINER_LOG_CONFIG_KEY_SIZE, tmp_key) == 0) {
            set_size = true;
        }
    }

    if (strcmp(spec->log_driver, CONTAINER_LOG_CONFIG_JSON_FILE_DRIVER) == 0) {
        ret = do_set_default_container_log_opts(set_file, set_rotate, set_size, p_info->id, root, spec);
    }

out:
    map_free(tag_maps);
    return ret;
}

static int update_container_log_config_to_container_spec(const struct logger_info *p_info, const char *runtime_root,
                                                         container_config *spec)
{
    int ret = 0;
    isulad_daemon_configs_container_log *daemon_container_opts = NULL;

    if (conf_get_container_log_opts(&daemon_container_opts) != 0) {
        return -1;
    }

    set_container_log_config_driver(daemon_container_opts, spec);

    if (spec->annotations == NULL) {
        spec->annotations = util_common_calloc_s(sizeof(json_map_string_string));
    }
    if (spec->annotations == NULL) {
        ERROR("Out of memory");
        ret = -1;
        goto out;
    }

    ret = merge_container_log_config_opts(daemon_container_opts->driver, daemon_container_opts->opts, spec);
    if (ret != 0) {
        goto out;
    }
    ret = do_parse_container_log_config_opts(p_info, runtime_root, spec);

out:
    free_isulad_daemon_configs_container_log(daemon_container_opts);
    return ret;
}

static int do_update_container_log_configs(char *id, char *name, char *image_name, char *image_id,
                                           const char *runtime_root, container_config *spec)
{
    struct logger_info l_info = { 0 };
    l_info.id = id;
    l_info.name = name;
    l_info.img_name = image_name;
    l_info.img_id = image_id != NULL ? image_id : image_name;
    l_info.daemon_name = "iSulad";

    return update_container_log_config_to_container_spec(&l_info, runtime_root, spec);
}

static container_config *get_container_spec(const container_create_request *request)
{
    container_config *container_spec = NULL;

    container_spec = get_container_spec_from_request(request);
    if (container_spec == NULL) {
        return NULL;
    }

    return container_spec;
}

static oci_runtime_spec *generate_oci_config(host_config *host_spec, const char *real_rootfs,
                                             container_config_v2_common_config *v2_spec)
{
    int ret = 0;
    oci_runtime_spec *oci_spec = NULL;

    oci_spec = default_spec(host_spec->system_container);
    if (oci_spec == NULL) {
        goto error_out;
    }

    ret = merge_all_specs(host_spec, real_rootfs, v2_spec, oci_spec);
    if (ret != 0) {
        ERROR("Failed to merge config");
        goto error_out;
    }

    if (merge_global_config(oci_spec) != 0) {
        ERROR("Failed to merge global config");
        goto error_out;
    }

#ifdef ENABLE_CDI
    ret = inject_CDI_devcies_for_oci_spec(oci_spec, host_spec);
    if (ret != 0) {
        ERROR("Failed to inject CDI devices");
        goto error_out;
    }
#endif /* ENABLE_CDI */

    return oci_spec;

error_out:
    free_oci_runtime_spec(oci_spec);
    return NULL;
}

static int merge_config_for_syscontainer(const container_create_request *request, const host_config *host_spec,
                                         const container_config *container_spec, const oci_runtime_spec *oci_spec)
{
    int ret = 0;
    char *value = NULL;

    if (!host_spec->system_container) {
        return 0;
    }
    if (request->rootfs == NULL) {
        value = oci_spec->root->path;
    } else {
        value = request->rootfs;
    }

    // should also update to container spec
    if (append_json_map_string_string(container_spec->annotations, "rootfs.mount", value)
        || append_json_map_string_string(oci_spec->annotations, "rootfs.mount", value)) {
        ERROR("Realloc annotations failed");
        ret = -1;
        goto out;
    }
    if (request->rootfs != NULL && (append_json_map_string_string(container_spec->annotations, "external.rootfs", "true")
                                    || append_json_map_string_string(oci_spec->annotations, "external.rootfs", "true"))) {
        ERROR("Realloc annotations failed");
        ret = -1;
        goto out;
    }

out:
    return ret;
}

static int inspect_image(const char *image, imagetool_image_summary **result)
{
    int ret = 0;
    im_summary_request *request = NULL;
    im_summary_response *response = NULL;

    if (image == NULL) {
        ERROR("Empty image name or id");
        return -1;
    }

    request = (im_summary_request *)util_common_calloc_s(sizeof(im_summary_request));
    if (request == NULL) {
        ERROR("Out of memory");
        return -1;
    }
    request->image.image = util_strdup_s(image);

    if (im_image_summary(request, &response) != 0) {
        if (response != NULL && response->errmsg != NULL) {
            ERROR("failed to inspect inspect image summary: %s", response->errmsg);
        } else {
            ERROR("Failed to call summary image");
        }
        ret = -1;
        goto cleanup;
    }

    if (response->image_summary != NULL) {
        *result = response->image_summary;
        response->image_summary = NULL;
    }

cleanup:
    free_im_summary_request(request);
    free_im_summary_response(response);
    return ret;
}

static int conf_get_image_id(const char *image, char **id)
{
    int ret = 0;
    imagetool_image_summary *ir = NULL;
    size_t len = 0;
    char *image_id = NULL;

    if (image == NULL || strcmp(image, "none") == 0) {
        *id = util_strdup_s("none");
        return 0;
    }

    if (inspect_image(image, &ir) != 0) {
        ERROR("Failed to inspect image status");
        ret = -1;
        goto out;
    }

    // it can guarantee that ir->id is not too long internally, and there is no need to judge overflow
    len = strlen("sha256:") + strlen(ir->id) + 1;
    image_id = (char *)util_smart_calloc_s(sizeof(char), len);
    if (image_id == NULL) {
        ERROR("Out of memory");
        ret = -1;
        goto out;
    }

    int nret = snprintf(image_id, len, "sha256:%s", ir->id);
    if (nret < 0 || (size_t)nret >= len) {
        ERROR("Failed to sprintf string");
        ret = -1;
        goto out;
    }

    *id = image_id;
    image_id = NULL;

out:
    free_imagetool_image_summary(ir);
    free(image_id);
    return ret;
}

static int register_new_container(const char *id, const char *image_id, const char *runtime, host_config *host_spec,
                                  container_config_v2_common_config *v2_spec,
                                  container_network_settings *network_settings)
{
    int ret = -1;
    bool registered = false;
    char *runtime_root = NULL;
    char *runtime_stat = NULL;
    container_t *cont = NULL;

    runtime_root = conf_get_routine_rootdir(runtime);
    if (runtime_root == NULL) {
        goto out;
    }

    runtime_stat = conf_get_routine_statedir(runtime);
    if (runtime_stat == NULL) {
        goto out;
    }

    cont = container_new(runtime, runtime_root, runtime_stat, image_id);
    if (cont == NULL) {
        ERROR("Failed to create container '%s'", id);
        goto out;
    }

    if (container_fill_v2_config(cont, v2_spec) != 0) {
        ERROR("Failed to fill v2 config");
        goto out;
    }

    if (container_fill_host_config(cont, host_spec) != 0) {
        ERROR("Failed to fill host config");
        goto out;
    }

    if (container_fill_state(cont, NULL) != 0) {
        ERROR("Failed to fill container state");
        goto out;
    }

    if (container_fill_restart_manager(cont) != 0) {
        ERROR("Failed to fill restart manager");
        goto out;
    }

#ifdef ENABLE_NETWORK
    // Allow network settings to be NULL for a temporary period, update it later
    if (network_settings != NULL && container_fill_network_settings(cont, network_settings) != 0) {
        ERROR("Failed to fill network settings");
        goto out;
    }
#endif

    if (container_fill_log_configs(cont) != 0) {
        ERROR("Failed to fill container log configs");
        goto out;
    }

    if (container_to_disk_locking(cont)) {
        ERROR("Failed to save container '%s'", id);
        goto out;
    }

    registered = containers_store_add(id, cont);
    if (!registered) {
        ERROR("Failed to register container '%s'", id);
        goto out;
    }

    ret = 0;

out:
    free(runtime_root);
    free(runtime_stat);
    if (ret != 0) {
        /* fail, do not use the input v2 spec, host spec and network settings, the memeory will be free by caller */
        if (cont != NULL) {
            cont->common_config = NULL;
            cont->hostconfig = NULL;
            cont->network_settings = NULL;
            container_unref(cont);
        }
    }
    return ret;
}

static int maintain_container_id(const container_create_request *request, char **out_id, char **out_name)
{
    int ret = 0;
    char *id = NULL;
    char *name = NULL;
    bool nret = false;
    bool skip_id_name_manage = false;

    if (request->id != NULL) {
        name = util_strdup_s(request->id);
    }

#ifdef ENABLE_CRI_API_V1
    // Under CRI API V1, that sandbox not nil and for pause indicates a pause pod
    // We maintain id and name manager in sandbox from upper level
    skip_id_name_manage = is_sandbox_container(request->sandbox);
    if (skip_id_name_manage) {
        id = util_strdup_s(request->sandbox->id);
        nret = id_name_manager_has_id_and_name(id, name);
    } else {
#endif
        if (name == NULL) {
            nret = id_name_manager_add_entry_with_new_id_and_name(&id, &name);
        } else {
            nret = id_name_manager_add_entry_with_new_id(name, &id);
        }
#ifdef ENABLE_CRI_API_V1
    }
#endif

    if (!nret) {
        __isula_auto_free char *used_id = NULL;
        used_id = container_name_index_get(name);
        if(used_id != NULL) {
            ERROR("Name %s is in use by container %s", name, used_id);
            isulad_set_error_message("Conflict. The name \"%s\" is already in use by container %s. "
                                    "You have to remove (or rename) that container to be able to reuse that name.",
                                    name, used_id);
        } else {
            ERROR("Failed to add entry to id name manager with new id and name");
            isulad_set_error_message("Failed to add entry to id name manager with new id and name");
        }
        ret = -1;
        goto out;
    }

    isula_libutils_set_log_prefix(id);

    EVENT("Event: {Object: %s, Type: Creating %s}", id, name);

    if (container_name_index_add(name, id)) {
        goto out;
    }

    if (!skip_id_name_manage && !id_name_manager_remove_entry(id, name)) {
        WARN("Failed to remove %s and %s from id name manager", id, name);
    }

    ERROR("Failed to add %s to container name index", name);
    isulad_set_error_message("Failed to add %s to container name index", name);
    ret = -1;

out:
    *out_id = id;
    *out_name = name;
    return ret;
}

static char *get_runtime_from_request(const container_create_request *request)
{
    return util_strings_to_lower(request->runtime);
}

static void pack_create_response(container_create_response *response, const char *id, uint32_t cc)
{
    response->cc = cc;
    if (g_isulad_errmsg != NULL) {
        response->errmsg = util_strdup_s(g_isulad_errmsg);
        DAEMON_CLEAR_ERRMSG();
    }
    if (id != NULL) {
        response->id = util_strdup_s(id);
    }
}

static int prepare_host_channel(const host_config_host_channel *host_channel, const char *user_remap)
{
    unsigned int host_uid = 0;
    unsigned int host_gid = 0;
    unsigned int size = 0;
    mode_t mode = HOST_PATH_MODE;
    int ret = 0;
    const char *userns_remap = user_remap;
#ifdef ENABLE_USERNS_REMAP
    char *daemon_userns_remap = conf_get_isulad_userns_remap();
    if (daemon_userns_remap != NULL) {
        userns_remap = (const char *)daemon_userns_remap;
    }
#endif

    if (host_channel == NULL || host_channel->path_on_host == NULL) {
        DEBUG("Host channel is not setting.");
        goto out;
    }
    if (util_dir_exists(host_channel->path_on_host)) {
        ERROR("Host path '%s' already exist", host_channel->path_on_host);
        ret = -1;
        goto out;
    }

#ifdef ENABLE_USERNS_REMAP
    if (daemon_userns_remap != NULL) {
        mode = HOST_PATH_MODE_USERNS_REMAP;
    }

    if (util_mkdir_p_userns_remap(host_channel->path_on_host, mode, userns_remap)) {
#else
    if (util_mkdir_p(host_channel->path_on_host, mode)) {
#endif
        ERROR("Failed to create host path '%s'.", host_channel->path_on_host);
        ret = -1;
        goto out;
    }
    if (userns_remap != NULL) {
        if (util_parse_user_remap(userns_remap, &host_uid, &host_gid, &size)) {
            ERROR("Failed to split string '%s'.", userns_remap);
            ret = -1;
            goto out;
        }
        if (chown(host_channel->path_on_host, host_uid, host_gid) != 0) {
            ERROR("Failed to chown host path '%s'.", host_channel->path_on_host);
            ret = -1;
            goto out;
        }
    }

out:
#ifdef ENABLE_USERNS_REMAP
    free(daemon_userns_remap);
#endif
    return ret;
}

static void umount_shm_by_configs(host_config *host_spec, container_config_v2_common_config *v2_spec)
{
    container_t *cont = NULL;

    cont = util_common_calloc_s(sizeof(container_t));
    if (cont == NULL) {
        ERROR("Out of memory");
        return;
    }
    cont->common_config = v2_spec;
    cont->hostconfig = host_spec;

    umount_share_shm(cont);

    cont->common_config = NULL;
    cont->hostconfig = NULL;

    free(cont);
}

static int create_container_root_dir(const char *id, const char *runtime_root)
{
    int ret = 0;
    int nret;
    char container_root[PATH_MAX] = { 0x00 };
    mode_t mask = umask(S_IWOTH);
#ifdef ENABLE_USERNS_REMAP
    char *userns_remap = conf_get_isulad_userns_remap();
#endif

    nret = snprintf(container_root, sizeof(container_root), "%s/%s", runtime_root, id);
    if ((size_t)nret >= sizeof(container_root) || nret < 0) {
        ret = -1;
        goto out;
    }
    // create container dir
    nret = util_mkdir_p(container_root, CONFIG_DIRECTORY_MODE);
    if (nret != 0 && errno != EEXIST) {
        SYSERROR("Failed to create container path %s", container_root);
        ret = -1;
        goto out;
    }

#ifdef ENABLE_USERNS_REMAP
    if (set_file_owner_for_userns_remap(container_root, userns_remap) != 0) {
        ERROR("Unable to change directory %s owner for user remap.", container_root);
        ret = -1;
        goto out;
    }
#endif

out:
    umask(mask);
#ifdef ENABLE_USERNS_REMAP
    free(userns_remap);
#endif
    return ret;
}

static int delete_container_root_dir(const char *id, const char *runtime_root)
{
    int ret = 0;
    char container_root[PATH_MAX] = { 0x00 };

    ret = snprintf(container_root, sizeof(container_root), "%s/%s", runtime_root, id);
    if ((size_t)ret >= sizeof(container_root) || ret < 0) {
        ERROR("Failed to sprintf invalid root directory %s/%s", runtime_root, id);
        ret = -1;
        goto out;
    }

    ret = util_recursive_rmdir(container_root, 0);
    if (ret != 0) {
        ERROR("Failed to delete container's state directory %s", container_root);
        ret = -1;
        goto out;
    }

out:
    return ret;
}

static host_config_host_channel *dup_host_channel(const host_config_host_channel *channel)
{
    host_config_host_channel *dup_channel = NULL;

    if (channel == NULL) {
        return NULL;
    }

    dup_channel = (host_config_host_channel *)util_common_calloc_s(sizeof(host_config_host_channel));
    if (dup_channel == NULL) {
        ERROR("Out of memory");
        return NULL;
    }

    dup_channel->path_on_host = channel->path_on_host != NULL ? util_strdup_s(channel->path_on_host) : NULL;
    dup_channel->path_in_container = channel->path_in_container != NULL ? util_strdup_s(channel->path_in_container) :
                                     NULL;
    dup_channel->permissions = channel->permissions != NULL ? util_strdup_s(channel->permissions) : NULL;
    dup_channel->size = channel->size;

    return dup_channel;
}

static int response_allocate_memory(container_create_response **response)
{
    if (response == NULL) {
        ERROR("Invalid NULL input");
        return -1;
    }

    *response = util_common_calloc_s(sizeof(container_create_response));
    if (*response == NULL) {
        ERROR("Out of memory");
        return -1;
    }

    return 0;
}

static int get_request_container_info(const container_create_request *request, char **id, char **name, uint32_t *cc)
{
    if (create_request_check(request) != 0) {
        ERROR("Invalid create container request");
        *cc = ISULAD_ERR_INPUT;
        return -1;
    }

    if (maintain_container_id(request, id, name) != 0) {
        *cc = ISULAD_ERR_EXEC;
        return -1;
    }

    return 0;
}

static int get_request_image_info(const container_create_request *request, char **image_type, char **image_name)
{
#ifdef ENABLE_REMOTE_IMAGE
    if (is_container_in_sandbox(request->sandbox) &&
        strcmp(request->sandbox->image_type, IMAGE_TYPE_REMOTE) == 0) {
        /*
         * Note: Currently, remote image type and coco image type 
         * are considered to be the same type.
         */ 
        *image_type = util_strdup_s(IMAGE_TYPE_REMOTE);
        *image_name = util_strdup_s(IMAGE_NAME_COCO);
        return 0;
    }
#endif
    *image_type = im_get_image_type(request->image, request->rootfs);
    if (*image_type == NULL) {
        return -1;
    }

    // Do not use none image because none image has no config.
    if (strcmp(request->image, "none") != 0 && strcmp(request->image, "none:latest") != 0) {
        *image_name = util_strdup_s(request->image);
    }

    // Check if config image exist if provided.
    if (*image_name != NULL) {
        if (!im_config_image_exist(*image_name)) {
            return -1;
        }
    }

    return 0;
}

static bool is_customized_runtime(const char* name)
{
    bool ret = true;
    struct service_arguments *args = NULL;
    defs_map_string_object_runtimes *runtimes = NULL;

    if (isulad_server_conf_rdlock()) {
        return false;
    }

    args = conf_get_server_conf();
    if (args == NULL) {
        ERROR("Failed to get isulad server config");
        ret = false;
        goto unlock_out;
    }

    if (args->json_confs != NULL) {
        runtimes = args->json_confs->runtimes;
    }
    if (runtimes == NULL) {
        ret = false;
        goto unlock_out;
    }

    size_t runtime_nums = runtimes->len;
    size_t i;
    for (i = 0; i < runtime_nums; i++) {
        if (strcmp(name, runtimes->keys[i]) == 0) {
            ret = true;
            goto unlock_out;
        }
    }
unlock_out:
    if (isulad_server_conf_unlock()) {
        ERROR("Failed to unlock isulad server config");
        ret = false;
    }

    return ret;
}

static int runtime_check(const char *name, bool *runtime_res)
{
    if (is_customized_runtime(name) || is_default_runtime(name)) {
        *runtime_res = true;
        return 0;
    }

    return -1;
}

static int preparate_runtime_environment(const container_create_request *request, const char *id, char **runtime,
                                         char **runtime_root, uint32_t *cc)
{
    bool runtime_res = false;

    if (util_valid_str(request->runtime)) {
        *runtime = get_runtime_from_request(request);
    } else {
        *runtime = conf_get_default_runtime();
    }

    if (*runtime == NULL) {
        *runtime = util_strdup_s(DEFAULT_RUNTIME_NAME);
    }

    if (runtime_check(*runtime, &runtime_res) != 0) {
        ERROR("Runtimes param check failed");
        *cc = ISULAD_ERR_EXEC;
        return -1;
    }

    if (!runtime_res) {
        ERROR("Invalid runtime name:%s", *runtime);
        isulad_set_error_message("Invalid runtime name (%s).", *runtime);
        *cc = ISULAD_ERR_EXEC;
        return -1;
    }

    *runtime_root = conf_get_routine_rootdir(*runtime);
    if (*runtime_root == NULL) {
        *cc = ISULAD_ERR_EXEC;
        return -1;
    }

    if (create_container_root_dir(id, *runtime_root) != 0) {
        *cc = ISULAD_ERR_EXEC;
        return -1;
    }

    return 0;
}

static int adapt_host_spec(host_config *host_spec, const sysinfo_t *sysinfo)
{
    int ret = 0;

    if (host_spec->memory > 0 && host_spec->memory_swap == 0 && sysinfo->cgmeminfo.swap) {
        if (host_spec->memory > (INT64_MAX / 2)) {
            ERROR("Memory swap out of range!");
            isulad_set_error_message("Memory swap out of range!");
            ret = -1;
            goto out;
        }
        host_spec->memory_swap = host_spec->memory * 2;
    }

out:
    return ret;
}

static int get_basic_spec(const container_create_request *request, host_config **host_spec,
                          container_config **container_spec, const sysinfo_t *sysinfo)
{
    *host_spec = get_host_spec(request, sysinfo);
    if (*host_spec == NULL) {
        return -1;
    }

    if (adapt_host_spec(*host_spec, sysinfo) != 0) {
        return -1;
    }

    *container_spec = get_container_spec(request);
    if (*container_spec == NULL) {
        return -1;
    }

    return 0;
}

static int do_image_create_container_roofs_layer(const char *container_id, const char *image_type,
                                                 const char *image_name, const char *mount_label, const char *rootfs,
                                                 json_map_string_string *storage_opt, char **real_rootfs)
{
    int ret = 0;
    im_prepare_request *request = NULL;

    request = util_common_calloc_s(sizeof(im_prepare_request));
    if (request == NULL) {
        ERROR("Out of memory");
        ret = -1;
        goto out;
    }
    request->container_id = util_strdup_s(container_id);
    request->image_name = util_strdup_s(image_name);
    request->image_type = util_strdup_s(image_type);
    request->mount_label = util_strdup_s(mount_label);
    request->rootfs = util_strdup_s(rootfs);
    if (storage_opt != NULL) {
        request->storage_opt = util_common_calloc_s(sizeof(json_map_string_string));
        if (request->storage_opt == NULL) {
            ERROR("Out of memory");
            ret = -1;
            goto out;
        }
        if (dup_json_map_string_string(storage_opt, request->storage_opt) != 0) {
            ERROR("Failed to dup map");
            ret = -1;
            goto out;
        }
    }

    if (im_prepare_container_rootfs(request, real_rootfs)) {
        ret = -1;
        goto out;
    }

out:
    free_im_prepare_request(request);
    return ret;
}

static int pack_security_config_to_v2_spec(const host_config *host_spec, container_config_v2_common_config *v2_spec)
{
    int ret = 0;
    bool no_new_privileges = false;
    char **label_opts = NULL;
    size_t label_opts_len = 0;
    char *seccomp_profile = NULL;
    char *process_label = NULL;
    char *mount_label = NULL;

    ret = parse_security_opt(host_spec, &no_new_privileges, &label_opts, &label_opts_len, &seccomp_profile);
    if (ret != 0) {
        ERROR("Failed to parse security opt");
        goto out;
    }

    v2_spec->seccomp_profile = seccomp_profile;
    seccomp_profile = NULL;
    v2_spec->no_new_privileges = no_new_privileges;
#ifdef ENABLE_SELINUX
    if (init_label((const char **)label_opts, label_opts_len, &process_label, &mount_label) != 0) {
        ERROR("Failed to append label");
        ret = -1;
        goto out;
    }
#endif

    v2_spec->mount_label = mount_label;
    mount_label = NULL;
    v2_spec->process_label = process_label;
    process_label = NULL;

out:
    util_free_array(label_opts);
    free(seccomp_profile);
    free(process_label);
    free(mount_label);
    return ret;
}

static void v2_spec_fill_basic_info(const char *id, const char *name, const char *image_name, const char *image_type,
                                    container_config *container_spec, container_config_v2_common_config *v2_spec)
{
    char timebuffer[TIME_STR_SIZE] = { 0 };
    v2_spec->id = id ? util_strdup_s(id) : NULL;
    v2_spec->name = name ? util_strdup_s(name) : NULL;
    v2_spec->image = image_name ? util_strdup_s(image_name) : util_strdup_s("none");
    v2_spec->image_type = image_type ? util_strdup_s(image_type) : NULL;
    (void)util_get_now_time_buffer(timebuffer, sizeof(timebuffer));
    free(v2_spec->created);
    v2_spec->created = util_strdup_s(timebuffer);
    v2_spec->config = container_spec;
}

static int create_v2_config_json(const char *id, const char *runtime_root, container_config_v2_common_config *v2_spec)
{
    int ret = 0;
    char *v2_json = NULL;
    parser_error err = NULL;
    container_config_v2 config_v2 = { 0 };
    container_state state = { 0 };

    config_v2.common_config = v2_spec;
    config_v2.state = &state;

    v2_json = container_config_v2_generate_json(&config_v2, NULL, &err);
    if (v2_json == NULL) {
        ERROR("Failed to generate container config V2 json string:%s", err ? err : " ");
        ret = -1;
        goto out;
    }

    ret = save_config_v2_json(id, runtime_root, v2_json);
    if (ret != 0) {
        ERROR("Failed to save container config V2 json to file");
        ret = -1;
        goto out;
    }

out:
    free(v2_json);
    free(err);
    return ret;
}

static int create_host_config_json(const char *id, const char *runtime_root, host_config *host_spec)
{
    int ret = 0;
    parser_error err = NULL;
    char *json_host_config = NULL;

    json_host_config = host_config_generate_json(host_spec, NULL, &err);
    if (json_host_config == NULL) {
        ERROR("Failed to generate container host config json string:%s", err ? err : " ");
        ret = -1;
        goto out;
    }

    ret = save_host_config(id, runtime_root, json_host_config);
    if (ret != 0) {
        ERROR("Failed to save container host config json to file");
        ret = -1;
        goto out;
    }

out:
    free(json_host_config);
    free(err);

    return ret;
}

#ifdef ENABLE_CRI_API_V1
static int dup_container_sandbox_info(const container_sandbox_info *src, container_sandbox_info **dest)
{
    __isula_auto_free char *json = NULL;
    __isula_auto_free parser_error err = NULL;

    if (src == NULL) {
        *dest = NULL;
        return 0;
    }

    json = container_sandbox_info_generate_json(src, NULL, &err);
    if (json == NULL) {
        ERROR("Failed to generate json: %s", err);
        return -1;
    }
    *dest = container_sandbox_info_parse_data(json, NULL, &err);
    if (*dest == NULL) {
        ERROR("Failed to parse json: %s", err);
        return -1;
    }

    return 0;
}

static int v2_spec_fill_sandbox_info(const container_sandbox_info *sandbox_info,
                                     container_config_v2_common_config *v2_spec)
{
    if (sandbox_info == NULL) {
        v2_spec->sandbox_info = NULL;
        return 0;
    }

    if (dup_container_sandbox_info(sandbox_info, &v2_spec->sandbox_info) != 0) {
        ERROR("Failed to dup sandbox info");
        return -1;
    }

    return 0;
}
#endif

static int save_container_config_before_create(const char *id, const char *runtime_root, host_config *host_spec,
                                               container_config_v2_common_config *v2_spec)
{
    if (create_v2_config_json(id, runtime_root, v2_spec) != 0) {
        return -1;
    }

    if (create_host_config_json(id, runtime_root, host_spec) != 0) {
        return -1;
    }

    return 0;
}

/*
 * request -> host_spec + container_spec
 * container_spec + image config
 * host_spec + container_spec + default_spec+ global_spec => oci_spec
 * verify oci_spec
 * register container(save v2_spec\host_spec\oci_spec)
 */
int container_create_cb(const container_create_request *request, container_create_response **response)
{
    uint32_t cc = ISULAD_SUCCESS;
    char *real_rootfs = NULL;
    char *image_type = NULL;
    char *runtime_root = NULL;
    char *image_id = NULL;
    char *oci_config_data = NULL;
    char *runtime = NULL;
    char *name = NULL;
    char *id = NULL;
    char *image_name = NULL;
    oci_runtime_spec *oci_spec = NULL;
    host_config *host_spec = NULL;
    container_config *container_spec = NULL;
    container_config_v2_common_config *v2_spec = NULL;
    host_config_host_channel *host_channel = NULL;
    container_network_settings *network_settings = NULL;
    int ret = 0;
    bool skip_id_name_manage = false;
    bool skip_sandbox_key_manage = false;
    __isula_auto_sysinfo_t sysinfo_t *sysinfo = NULL;
#ifdef ENABLE_CRI_API_V1
    cri_container_message_t message;
#endif

    DAEMON_CLEAR_ERRMSG();

    if (response_allocate_memory(response) != 0) {
        return -1;
    }

    if (get_request_container_info(request, &id, &name, &cc) != 0) {
        goto pack_response;
    }

    if (get_request_image_info(request, &image_type, &image_name) != 0) {
        cc = ISULAD_ERR_EXEC;
        goto clean_nameindex;
    }

    if (preparate_runtime_environment(request, id, &runtime, &runtime_root, &cc) != 0) {
        goto clean_nameindex;
    }

    sysinfo = get_sys_info(true);
    if (sysinfo == NULL) {
        ERROR("Failed to get system info");
        cc = ISULAD_ERR_EXEC;
        goto clean_nameindex;
    }

    if (get_basic_spec(request, &host_spec, &container_spec, sysinfo) != 0) {
        cc = ISULAD_ERR_INPUT;
        goto clean_container_root_dir;
    }
    // update runtime of host config
    free(host_spec->runtime);
    host_spec->runtime = util_strdup_s(runtime);

    v2_spec = util_common_calloc_s(sizeof(container_config_v2_common_config));
    if (v2_spec == NULL) {
        ERROR("Failed to malloc container_config_v2_common_config");
        cc = ISULAD_ERR_INPUT;
        goto clean_container_root_dir;
    }

    v2_spec_fill_basic_info(id, name, image_name, image_type, container_spec, v2_spec);

#ifdef ENABLE_CRI_API_V1
    if (v2_spec_fill_sandbox_info(request->sandbox, v2_spec) != 0) {
        ERROR("Failed to fill sandbox info");
        cc = ISULAD_ERR_INPUT;
        goto clean_container_root_dir;
    }

    // Under cri_api_v1, that sandbox is not nil and is_pause indicates that
    // this is a pause container itself, sandbox will manage id and name, so we skip here.
    // For pause container under cni network mode, networksettings will also be
    // maintained by sandbox, so we skip here and allow networksettings to be nil for a temporary period.
    skip_id_name_manage = is_sandbox_container(request->sandbox);
    skip_sandbox_key_manage = (is_sandbox_container(request->sandbox) && namespace_is_cni(host_spec->network_mode));
#endif

#ifdef ENABLE_NRI
    if (request->adjust != NULL && nri_adjust_host_spec(request->adjust, host_spec) != 0) {
        ERROR("Failed to adjust host spec");
        cc = ISULAD_ERR_INPUT;
        goto clean_container_root_dir;
    }
#endif

    if (save_container_config_before_create(id, runtime_root, host_spec, v2_spec) != 0) {
        ERROR("Failed to malloc container_config_v2_common_config");
        cc = ISULAD_ERR_INPUT;
        goto clean_container_root_dir;
    }

    if (pack_security_config_to_v2_spec(host_spec, v2_spec) != 0) {
        ERROR("Failed to pack security config");
        cc = ISULAD_ERR_INPUT;
        goto clean_container_root_dir;
    }

    if (init_container_network_confs(id, runtime_root, host_spec, v2_spec) != 0) {
        ERROR("Init Network files failed");
        cc = ISULAD_ERR_INPUT;
        goto clean_container_root_dir;
    }

    ret = do_image_create_container_roofs_layer(id, image_type, image_name, v2_spec->mount_label, request->rootfs,
                                                host_spec->storage_opt, &real_rootfs);
    if (ret != 0) {
        ERROR("Can not create container %s rootfs layer", id);
        cc = ISULAD_ERR_EXEC;
        goto clean_container_root_dir;
    }

    ret = im_merge_image_config(image_type, image_name, v2_spec->config);
    if (ret != 0) {
        ERROR("Can not merge container_spec with image config");
        cc = ISULAD_ERR_EXEC;
        goto clean_rootfs;
    }

    if (strcmp(v2_spec->image_type, IMAGE_TYPE_OCI) == 0) {
        if (conf_get_image_id(v2_spec->image, &image_id) != 0) {
            cc = ISULAD_ERR_EXEC;
            goto clean_rootfs;
        }
    }

    if (do_update_container_log_configs(id, name, image_name, image_id, runtime_root, v2_spec->config)) {
        cc = ISULAD_ERR_EXEC;
        goto clean_rootfs;
    }

    if (verify_container_config(v2_spec->config, runtime) != 0) {
        cc = ISULAD_ERR_EXEC;
        goto clean_rootfs;
    }

    oci_spec = generate_oci_config(host_spec, real_rootfs, v2_spec);
    if (oci_spec == NULL) {
        cc = ISULAD_ERR_EXEC;
        goto umount_shm;
    }

    if (!skip_sandbox_key_manage) {
        network_settings = generate_network_settings(host_spec);
        if (network_settings == NULL) {
            ERROR("Failed to generate network settings");
            cc = ISULAD_ERR_EXEC;
            goto umount_shm;
        }
    }

    if (merge_config_for_syscontainer(request, host_spec, v2_spec->config, oci_spec) != 0) {
        ERROR("Failed to merge config for syscontainer");
        cc = ISULAD_ERR_EXEC;
        goto clean_netns;
    }

    // merge hostname, resolv.conf, hosts, required for all container
    if (merge_network(host_spec, request->rootfs, runtime_root, id, container_spec->hostname) != 0) {
        ERROR("Failed to merge network config");
        cc = ISULAD_ERR_EXEC;
        goto clean_netns;
    }

#ifdef ENABLE_PLUGIN
    /* modify oci_spec by plugin. */
    if (plugin_event_container_pre_create(id, oci_spec) != 0) {
        ERROR("Plugin event pre create failed");
        (void)plugin_event_container_post_remove2(id, oci_spec); /* ignore error */
        cc = ISULAD_ERR_EXEC;
        goto clean_netns;
    }
#endif

#ifdef ENABLE_NRI
    // modify oci spec by nri plugin
    if (request->adjust != NULL && nri_adjust_oci_spec(request->adjust, oci_spec) != 0) {
        ERROR("Failed to adjust oci spec");
        cc = ISULAD_ERR_EXEC;
        goto clean_netns;
    }
#endif

    host_channel = dup_host_channel(host_spec->host_channel);
    if (prepare_host_channel(host_channel, host_spec->user_remap)) {
        ERROR("Failed to prepare host channel");
        cc = ISULAD_ERR_EXEC;
        goto clean_netns;
    }

    if (verify_container_settings(oci_spec, sysinfo) != 0) {
        ERROR("Failed to verify container settings");
        cc = ISULAD_ERR_EXEC;
        goto umount_channel;
    }

    if (container_v2_spec_merge_container_spec(v2_spec) != 0) {
        ERROR("Failed to merge container settings");
        cc = ISULAD_ERR_EXEC;
        goto umount_channel;
    }

    if (save_oci_config(id, runtime_root, oci_spec) != 0) {
        ERROR("Failed to save container settings");
        cc = ISULAD_ERR_EXEC;
        goto umount_channel;
    }

    if (register_new_container(id, image_id, runtime, host_spec, v2_spec, network_settings)) {
        ERROR("Failed to register new container");
        cc = ISULAD_ERR_EXEC;
        goto umount_channel;
    }
    host_spec = NULL;
    v2_spec = NULL;
    network_settings = NULL;

    EVENT("Event: {Object: %s, Type: Created %s}", id, name);
    (void)isulad_monitor_send_container_event(id, CREATE, -1, 0, NULL, NULL);
#ifdef ENABLE_CRI_API_V1
    if (is_container_in_sandbox(request->sandbox)) {
        message.container_id = id;
        message.sandbox_id = request->sandbox->id;
        message.type = CRI_CONTAINER_MESSAGE_TYPE_CREATED;
        mailbox_publish(MAILBOX_TOPIC_CRI_CONTAINER, &message);
    }
#endif
    goto pack_response;

umount_channel:
    umount_host_channel(host_channel);
clean_netns:
#ifdef ENABLE_NATIVE_NETWORK
    if (util_native_network_checker(host_spec->network_mode) || namespace_is_cni(host_spec->network_mode)) {
#else
    if (namespace_is_cni(host_spec->network_mode)) {
#endif
        if (network_settings != NULL) {
            (void)remove_network_namespace_file(network_settings->sandbox_key);
        }
    }
umount_shm:
    umount_shm_by_configs(host_spec, v2_spec);

    (void)release_volumes(v2_spec->mount_points, id, true);

clean_rootfs:
    (void)im_remove_container_rootfs(image_type, id);

clean_container_root_dir:
    (void)delete_container_root_dir(id, runtime_root);

clean_nameindex:
    container_name_index_remove(name);
    if (!skip_id_name_manage) {
        id_name_manager_remove_entry(id, name);
    }

pack_response:
    pack_create_response(*response, id, cc);
    free(runtime);
    free(oci_config_data);
    free(runtime_root);
    free(real_rootfs);
    free(image_type);
    free(image_name);
    free(name);
    free(image_id);
    free(id);
    free_oci_runtime_spec(oci_spec);
    free_host_config(host_spec);
    free_container_config_v2_common_config(v2_spec);
    free_host_config_host_channel(host_channel);
    free_container_network_settings(network_settings);
    isula_libutils_free_log_prefix();
    malloc_trim(0);
    return (cc == ISULAD_SUCCESS) ? 0 : -1;
}
