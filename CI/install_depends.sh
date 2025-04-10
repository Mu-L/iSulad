#!/bin/sh
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
##- @Description: generate cetification
##- @Author: lifeng
##- @Create: 2020-03-30
#######################################################################
set -xe
umask 0022

builddir=`env | grep BUILDDIR | awk -F '=' '{print $2}'`
if [ "x$builddir" == "x" ];then
    builddir=/usr/local
fi

buildstatus=${builddir}/build.fail

declare -a buildlogs
build_log_crictl=${builddir}/build.crictl.log
build_log_cni_plugins=${builddir}/build.cni_plugins.log
build_log_cni_dnsname=${builddir}/build.cni_dnsname.log
buildlogs+=(${build_log_crictl} ${build_log_cni_plugins} ${build_log_cni_dnsname})

mkdir -p ${builddir}/bin
mkdir -p ${builddir}/include
mkdir -p ${builddir}/lib
mkdir -p ${builddir}/lib/pkgconfig
mkdir -p ${builddir}/systemd/system

git config --global http.postBuffer 524288000

#install crictl
function make_crictl()
{
    cd ~
    git clone https://gitee.com/duguhaotian/cri-tools.git
    go version
    cd cri-tools
    # update crictl to 1.25, used to test cri v1 alpha and cri v1.
    git checkout v1.25.0
    make -j $nproc
    echo "make cri-tools: $?"
    cp ./build/bin/crictl ${builddir}/bin/
}

#install cni plugins
function make_cni_plugins()
{
    if [ -e /usr/libexec/cni ]; then
       rm -f ${builddir}/cni/bin/*
       mkdir -p ${builddir}/cni/bin/
       cp /usr/libexec/cni/* ${builddir}/cni/bin/
       return 0
    fi
    cd ~
    git clone https://gitee.com/duguhaotian/plugins.git
    cd plugins
    git checkout v1.2.0
    ./build_linux.sh
    mkdir -p ${builddir}/cni/bin/
    cp bin/* ${builddir}/cni/bin/
}

#install cni dnsname
function make_cni_dnsname()
{
    cd ~
    git clone https://gitee.com/zh_xiaoyu/dnsname.git
    cd dnsname
    make
    mkdir -p ${builddir}/cni/bin/
    cp bin/* ${builddir}/cni/bin/
}

function check_make_status()
{
    set +e
    script_cmd="$1"
    log_file="$2"
    ${script_cmd} >${log_file} 2>&1
    if [ $? -ne 0 ];then
        cat ${log_file}
        touch ${buildstatus}
    fi
    rm -f $2
    set -e
}

rm -rf ${buildstatus}
check_make_status make_crictl ${build_log_crictl} &
check_make_status make_cni_plugins ${build_log_cni_plugins} &
check_make_status make_cni_dnsname ${build_log_cni_dnsname} &

# install lxc
cd ~
git clone https://gitee.com/src-openeuler/lxc.git
git config --global --add safe.directory ~/lxc/lxc-5.0.2
cd lxc
./apply-patches
cd lxc-5.0.2
mkdir -p build
sed -i 's/fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO/fd == 0 || fd == 1 || fd == 2 || fd >= 1000/g' ./src/lxc/start.c
meson setup -Disulad=true -Dprefix=${builddir} build
meson compile -C build
make install
ldconfig

# install lcr
cd ~
git clone https://gitee.com/openeuler/lcr.git
cd lcr
sed -i 's/fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO/fd == 0 || fd == 1 || fd == 2 || fd >= 1000/g' ./src/utils/utils_file.c
mkdir -p build
cd build
cmake -DENABLE_UT=ON -DLIB_INSTALL_DIR=${builddir}/lib -DCMAKE_INSTALL_PREFIX=${builddir} ../
make -j $(nproc)
make install
cd -
ldconfig

# install runc
cd ~
if [ -d ./runc ];then
    rm -rf ./runc
fi
git clone https://gitee.com/src-openeuler/runc.git
cd runc
tfname=$(cat runc.spec | grep Source0 | awk -F '/' '{print $NF}')
tar -zxf ${tfname}
runc_dir=$(tar -tf ${tfname} | head -1)
cd ${runc_dir}
export GO111MODULE=off
export GOPATH=`pwd`/.gopath
mkdir -p .gopath/src/github.com/opencontainers
if [ -L .gopath/src/github.com/opencontainers/runc ];then
    echo "Link exist"
else
    ln -sf `pwd` .gopath/src/github.com/opencontainers/runc
fi

cd .gopath/src/github.com/opencontainers/runc
make BUILDTAGS='seccomp selinux'
cp -f ./runc ${builddir}/bin
cd -

# install lib-shim-v2
source $HOME/.cargo/env
cd ~
rm -rf lib-shim-v2
git clone https://gitee.com/src-openeuler/lib-shim-v2.git
cd lib-shim-v2
tar xf lib-shim-v2-*
dname=$(tar -tf lib-shim-v2-*.tar.gz | head -1)
cd $dname
git apply ../0*
mkdir .cargo
cat >> ./.cargo/config << EOF
[source.crates-io]
replace-with = "local-registry"
[source.local-registry]
directory = "vendor"
EOF
cargo build --release
make install
cd -
ldconfig

# install cricli
cd ~
git clone https://gitee.com/jingwoo/cricli.git
cd cricli
make -j $(nproc)
cp cricli ${builddir}/bin
cd -

wait
if [ -e ${buildstatus} ];then
    for i in ${buildlogs[@]}
    do
        if [ -e ${i} ];then
            cat $i
        fi
    done
    exit 1
fi
