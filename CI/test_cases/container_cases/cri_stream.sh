#!/bin/bash
#
# attributes: isulad cri websockets exec attach
# concurrent: NA
# spend time: 46

curr_path=$(dirname $(readlink -f "$0"))
data_path=$(realpath $curr_path/criconfigs)
pause_img_path=$(realpath $curr_path/test_data)
source ../helpers.sh

# $1 : retry limit
# $2 : retry_interval
# $3 : retry function
function do_retry()
{
    for i in $(seq 1 "$1"); do
        $3
        if [ $? -ne 0 ]; then
            return 0
        fi
        sleep $2
    done
    return 1
}

function get_ioCopy()
{
    ps -T -p $(cat /var/run/isulad.pid) | grep IoCopy
    return $?
}

function do_pre()
{
    local ret=0
    local image="busybox"
    local podimage="mirrorgooglecontainers/pause-amd64"
    local test="set_up => (${FUNCNAME[@]})"

    msg_info "${test} starting..."

    init_cri_conf $1 "without_valgrind"
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - failed to init cri conf: ${1}" && return ${FAILURE}

    crictl pull ${image}
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - failed to pull image: ${image}" && return ${FAILURE}

    crictl images | grep ${podimage}
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - missing list image: ${podimage}" && ((ret++))

    return ${ret}
}

function set_up()
{
    local ret=0
    sid=$(crictl runp --runtime $1 ${data_path}/sandbox-config.json)
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - failed to run sandbox" && ((ret++))

    cid=$(crictl create $sid ${data_path}/container-config.json ${data_path}/sandbox-config.json)
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - failed to create container" && ((ret++))

    crictl start $cid
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - failed to start container" && ((ret++))

    msg_info "${test} finished with return ${ret}..."
    return ${ret}
}

function test_cri_exec_fun()
{
    local ret=0
    local retry_limit=20
    local retry_interval=1
    local test="test_cri_exec_fun => (${FUNCNAME[@]})"
    msg_info "${test} starting..."
    declare -a fun_pids
    for index in $(seq 1 20); do
            nohup cricli exec -it ${cid} date &
            fun_pids[${#pids[@]}]=$!
    done
    wait ${fun_pids[*]// /|}

    declare -a abn_pids
    for index in $(seq 1 20); do
            nohup cricli exec -it ${cid} xxx &
            abn_pids[${#pids[@]}]=$!
    done
    wait ${abn_pids[*]// /|}

    do_retry ${retry_limit} ${retry_interval} get_ioCopy
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - residual IO copy thread in CRI exec operation" && ((ret++))

    msg_info "${test} finished with return ${ret}..."
    return ${ret}
}

function test_cri_exec_abn
{
    local ret=0
    local retry_limit=20
    local retry_interval=1
    local test="test_cri_exec_abn => (${FUNCNAME[@]})"
    msg_info "${test} starting..."

    nohup cricli exec -it ${cid} sleep 100 &
    pid=$!
    sleep 3
    kill -9 $pid

    do_retry ${retry_limit} ${retry_interval} get_ioCopy
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - residual IO copy thread in CRI exec operation" && ((ret++))

    msg_info "${test} finished with return ${ret}..."
    return ${ret}
}

function test_cri_attach
{
    local ret=0
    local test="test_cri_attach => (${FUNCNAME[@]})"
    msg_info "${test} starting..."

    nohup cricli attach -ti ${cid} &
    pid=$!
    sleep 2

    ps -T -p $(cat /var/run/isulad.pid) | grep IoCopy
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - attach failed, no IOCopy thread" && ((ret++))

    kill -9 $pid
    sleep 2

    ps -T -p $(cat /var/run/isulad.pid) | grep IoCopy
    [[ $? -eq 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - residual IO copy thread in CRI attach operation" && ((ret++))

    msg_info "${test} finished with return ${ret}..."
    return ${ret}
}

function tear_down()
{
    local ret=0

    crictl stop $cid
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - failed to stop container" && ((ret++))

    crictl rm $cid
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - failed to rm container" && ((ret++))

    crictl stopp $sid
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - failed to stop sandbox" && ((ret++))

    crictl rmp $sid
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - failed to rm sandbox" && ((ret++))

    return ${ret}
}

function do_post()
{
    local ret=0
    restore_cri_conf "without_valgrind"
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - failed to restore cri conf" && ((ret++))
    return $ret
}

function do_test_t()
{
    local ret=0
    local runtime=$1
    local test="cri_stream_test => (${runtime})"
    msg_info "${test} starting..."

    set_up $runtime || ((ret++))

    test_cri_exec_fun  || ((ret++))
    test_cri_exec_abn || ((ret++))

    test_cri_attach || ((ret++))

    tear_down || ((ret++))

    msg_info "${test} finished with return ${ret}..."

    return $ret
}

declare -i ans=0

for version in ${CRI_LIST[@]};
do
    test="test_cri_stream_fun, use cri version => (${version})"
    msg_info "${test} starting..."

    do_pre $version || ((ans++))

    for element in ${RUNTIME_LIST[@]};
    do
        do_test_t $element || ((ans++))
    done

    do_post || ((ans++))
    msg_info "${test} finished with return ${ans}..."
done

show_result ${ans} "${curr_path}/${0}"
