#!/bin/bash

#######################################################################
##- Copyright (c) Huawei Technologies Co., Ltd. 2020. All rights reserved.
# - iSulad licensed under the Mulan PSL v2.
# - You can use this software according to the terms and conditions of the Mulan PSL v2.
# - You may obtain a copy of Mulan PSL v2 at:
# -     http://license.coscl.org.cn/MulanPSL2
# - THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# - IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
# - PURPOSE.
# - See the Mulan PSL v2 for more details.
##- @Description:CI
##- @Author: lifeng
##- @Create: 2020-03-30
#######################################################################

# testcase result
TC_RET_T=0
declare -a lines

# Root directory of integration tests.
LCR_ROOT_PATH="/var/lib/isulad/engines/lcr"
RUNTIME_ROOT_PATH="/var/lib/isulad/engines"
valgrind_log="/tmp/valgrind.log"
ISUALD_LOG="/var/lib/isulad/isulad.log"
ISULAD_ROOT_PATH="/var/lib/isulad"
ISULAD_RUN_ROOT_PATH="/var/run/isulad"

RUNTIME_LIST=(lcr runc)

DEFAULT_RUNTIME=runc

CRI_LIST=(v1alpha v1)

testcase_data="/tmp/testcases_data"

enable_native_network=0

declare -r -i FAILURE=1

function is_overlay_driver() {
    isula info | grep "Storage Driver" | grep "overlay"
    return $?
}

function cut_output_lines() {
    message=`$@ 2>&1`
    retval=$?
    oldifs=${IFS}
    IFS=$'\n'
    lines=(${message})
    IFS="${oldifs}"
    return $retval
}

# use string compare to check the result
function fn_check_eq() {
    if [ "x$1" != "x$2" ];then
        echo "$3"
        TC_RET_T=$(($TC_RET_T+1))
    fi
}

function fn_check_ne() {
    if [[ "x$1" == "x$2" ]];then
        echo "$3"
        TC_RET_T=$(($TC_RET_T+1))
    fi
}

function testcontainer() {
    st=`isula inspect -f '{{json .State.Status}}' $3 "$1"`
    if ! [[ "${st}" =~ "$2" ]];then
        echo "expect status $2, but get ${st}"
        TC_RET_T=$(($TC_RET_T+1))
    fi
}

function crictl() {
    CRICTL=$(which crictl)
    # Default timeout is 2s.
    # In some high IO testcase, isulad handle CRI request time maybe more than 2s.
    # And the crictl will print error message "context deadline exceeded"
    "$CRICTL" -i unix:///var/run/isulad.sock -r unix:///var/run/isulad.sock --timeout 5s "$@"
}

function msg_ok()
{
    echo -e "[$(date +'%Y-%m-%dT%H:%M:%S%z')]: \033[1;32m$@\033[0m"
}

function msg_err()
{
    echo -e "[$(date +'%Y-%m-%dT%H:%M:%S%z')]: \033[1;31m$@\033[0m" >&2
}

function msg_info()
{
    echo "[$(date +'%Y-%m-%dT%H:%M:%S%z')]: $@"
}

function show_result() {
    [[ ${1} -ne 0 ]] && msg_err "TESTSUIT: $2 FAILED" && return ${FAILURE}
    msg_ok "TESTSUIT: $2 SUCCESS"
}

function wait_isulad_running() {
    echo "-------waiting iSulad running--------"
    waitcnt=0
    maxcnt=60
    while [ 0 ]
    do
        isula version
        if [ $? -eq 0 ];then
            break
        fi
        if [ $waitcnt -gt $maxcnt ];then
            echo "iSulad is not running after ${maxcnt}s"
            tail $ISUALD_LOG
            return 1
        fi
        waitcnt=$(($waitcnt+1))
        sleep 1
    done
    echo "--------iSulad is running-----------"
}

function start_isulad_with_valgrind() {
    valgrind --fair-sched=yes --log-file=$valgrind_log --tool=memcheck --leak-check=yes -v --track-origins=yes isulad $@ -l DEBUG >/dev/null 2>&1 &
    wait_isulad_running
}

function check_isulad_stopped() {
    maxtimes=15
    curcnt=0

    spid=$1
    while [ $curcnt -lt $maxtimes ]
    do
        ps aux | grep isulad | grep -v "grep" | grep -w $spid
        if [ $? -ne 0 ];then
            return 0
        fi
        let "curcnt=$curcnt + 1"
        sleep 1
    done
    return 1
}

function check_valgrind_log() {
    pid=`cat /var/run/isulad.pid`
    kill -15 $pid
    check_isulad_stopped $pid
    if [ $? -ne 0 ];then
        echo "Stop iSulad with valgrind failed"
        kill -9 $pid
        sleep 1
    fi

    cat $valgrind_log | grep "are definitely lost" | grep "==$pid=="
    if [ $? -eq 0 ];then
        echo "Memory leak may checked by valgrind, see valgrind log file: $valgrind_log"
        cat $valgrind_log
        exit 1
    fi
    return 0
}

function start_isulad_without_valgrind() {
    isulad $@ -l DEBUG > /dev/null 2>&1 &
    wait_isulad_running
}

function stop_isulad_without_valgrind() {
    pid=$(cat /var/run/isulad.pid)
    kill -15 $pid
    check_isulad_stopped $pid
    if [[ $? -ne 0 ]]; then
        echo "isulad process is still alive"
        return 1
    fi
    return 0
}

function init_cni_conf()
{
    dtpath="$1"
    mkdir -p /etc/cni/net.d/
    rm -rf /etc/cni/net.d/*
    mkdir -p /opt/cni/bin
    cp $dtpath/bins/* /opt/cni/bin/
    cp $dtpath/good.conflist /etc/cni/net.d/
    ls /etc/cni/net.d/

    check_valgrind_log
    if [ $? -ne 0 ]; then
        echo "stop isulad failed"
        TC_RET_T=$(($TC_RET_T+1))
    fi

    start_isulad_with_valgrind --network-plugin cni
    if [ $? -ne 0 ]; then
        echo "start failed"
        TC_RET_T=$(($TC_RET_T+1))
    fi

    return $TC_RET_T
}

function do_install_thinpool()
{
    local ret=0

    cat /etc/isulad/daemon.json | grep driver | grep devicemapper
    if [[ $? -ne 0 ]]; then
        return ${ret}
    fi

    dev_disk=`pvs | grep isulad | awk '{print$1}'`
    rm -rf /var/lib/isulad/*
    dmsetup remove_all
    lvremove -f isulad/thinpool
    lvremove -f isulad/thinpoolmeta
    vgremove -f isulad
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - vgremove failed" && ((ret++))

    pvremove -f $dev_disk
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - pvremove failed" && ((ret++))

    # If udev do not sync in time, do remove force
    rm -rf /dev/isulad

    mount | grep $dev_disk | grep /var/lib/isulad
    if [ x"$?" == x"0" ]; then
        umount /var/lib/isulad
        [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - umount isulad failed" && ((ret++))
    fi
    touch /etc/lvm/profile/isulad-thinpool.profile
    cat > /etc/lvm/profile/isulad-thinpool.profile <<EOF
activation {
thin_pool_autoextend_threshold=80
thin_pool_autoextend_percent=20
}
EOF
    echo y | mkfs.ext4 $dev_disk
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - mkfs.ext4 $dev_disk failed" && ((ret++))

    pvcreate -y $dev_disk
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - vgremove isulad failed" && ((ret++))

    vgcreate isulad $dev_disk
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - vgremove isulad failed" && ((ret++))

    echo y | lvcreate --wipesignatures y -n thinpool isulad -l 80%VG
    echo y | lvcreate --wipesignatures y -n thinpoolmeta isulad -l 1%VG

    dmsetup status | grep -w "isulad-thinpoolmeta"
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - isulad-thinpoolmeta: no such device" && ((ret++))

    dmsetup status | grep -w "isulad-thinpool"
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - isulad-thinpool: no such device" && ((ret++))

    lvconvert -y --zero n -c 512K --thinpool isulad/thinpool --poolmetadata isulad/thinpoolmeta
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - lvconvert failed" && ((ret++))

    lvchange --activate ay isulad

    lvchange --metadataprofile isulad-thinpool isulad/thinpool
    [[ $? -ne 0 ]] && msg_err "${FUNCNAME[0]}:${LINENO} - lvchange failed" && ((ret++))

    lvs -o+seg_monitor

    return $ret
}

# Delete all containers and stop isulad before executing this func
function reinstall_thinpool()
{
    retry_limit=10
    retry_interval=2
    state="fail"

    for i in $(seq 1 "$retry_limit"); do
        do_install_thinpool
        if [ $? -eq 0 ]; then
            state="success"
            break;
        fi
        sleep $retry_interval
    done

    if [ "$state" != "success" ]; then
        return 1
    fi
    return 0
}

function wait_container_status()
{
    local cont="$1"
    local status="$2"
    local count=1
    local flag=1
    while [ $count -le 10 ]
    do
        local st=$(isula inspect -f {{.State.Status}} ${cont})
        if [ "x$st" == "x$status" ]
        then
            return 0
        fi
        sleep 1
        ((count++))
    done
    return 1
}

function do_pretest() {
    msg_info "#### do pretest #####"
    isula ps -a
    isula network ls
    enable_native_network=$?
    msg_info "#####################"
}

function init_cri_conf() {
    local ret=0
    check_valgrind_log
    if [ $? -ne 0 ]; then
        echo "stop isulad failed"
        ret=$(($ret+1))
    fi

    cp /etc/isulad/daemon.json /etc/isulad/daemon.bak
    if [ "x$1" == "xv1" ]; then
        echo "use cri v1"
        sed -i '/"pod-sandbox-image": ""/i "enable-cri-v1": true,' /etc/isulad/daemon.json
    fi
    sed -i "s#\"pod-sandbox-image\": \"\"#\"pod-sandbox-image\": \"mirrorgooglecontainers/pause-amd64:3.0\"#g" /etc/isulad/daemon.json

    if [ "x$2" == "xwithout_valgrind" ]; then
        start_isulad_without_valgrind
    else
        echo "start with valgrind"
        start_isulad_with_valgrind   
    fi
    if [ $? -ne 0 ]; then
        echo "start failed"
        ret=$(($ret+1))
    fi

    isula load -i ${pause_img_path}/pause.tar
    if [ $? -ne 0 ]; then
        msg_err "Failed to load pause image"
        ret=$(($ret + 1))
        return $ret
    fi

    return $ret
}

function restore_cri_conf() {
    local ret=0

    if [ "x$1" == "xwithout_valgrind" ]; then
        stop_isulad_without_valgrind
    else
        echo "stop with valgrind"
        check_valgrind_log
    fi
    if [ $? -ne 0 ]; then
        echo "stop isulad failed"
        ret=$(($ret+1))
    fi

    cp /etc/isulad/daemon.bak /etc/isulad/daemon.json

    start_isulad_with_valgrind
    if [ $? -ne 0 ]; then
        echo "start failed"
        ret=$(($ret+1))
    fi

    return $ret
}

do_pretest
