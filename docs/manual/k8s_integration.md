# kubernetes integration

## Configuration

1. Configure `isulad`

   Configure the `pod-sandbox-image`  in `/etc/isulad/daemon.json`:

   ```json
   "pod-sandbox-image": "my-pause:1.0.0"
   ```

   Configure the `endpoint`of `isulad`:

   ```json
   "hosts": [
           "unix:///var/run/isulad.sock"
       ]
   ```

   if `hosts` is not configured, the default endpoint is `unix:///var/run/isulad.sock`.

   `iSulad` supports both `CRI V1alpha2` and `CRI V1`, and uses `CRI V1alph2` by default.
   If `CRI V1` is required, it can be configured in `/etc/isulad/daemon.json` to enable `CRI V1`:

   ```json
       "enable-cri-v1": true,
   ```

   If `iSulad` is compiled from source codes, `-D ENABLE_CRI_API_V1=ON` option is required in cmake.

2. Restart `isulad`:

   ```bash
   $ sudo systemctl restart isulad
   ```

3. Start `kubelet` based on the configuration or default value:

   ```bash
   $ /usr/bin/kubelet 
   --container-runtime-endpoint=unix:///var/run/isulad.sock
   --image-service-endpoint=unix:///var/run/isulad.sock 
   --pod-infra-container-image=my-pause:1.0.0
   --container-runtime=remote
   ...
   ```

## Use  RuntimeClass

RuntimeClass is a feature for selecting the container runtime configuration. The container runtime configuration is used to run a Pod's containers. For more information, please refer to [runtime-class](https://kubernetes.io/docs/concepts/containers/runtime-class/). Currently `isulad` only supports `kata-containers` and `runc`.

1. Configure `isulad` in `/etc/isulad/daemon.json`:

   ```json
   "runtimes": {
           "kata-runtime": {
               "path": "/usr/bin/kata-runtime",
               "runtime-args": [
                   "--kata-config",
                   "/usr/share/defaults/kata-containers/configuration.toml"
               ]
           }
       }
   ```

2. Extra configuration

   `iSulad` supports the `overlay2` and `devicemapper` as storage drivers. The default value is `overlay2`.

   In some scenarios, using block device type as storage drivers is a better choice, such as run a `kata-containers`. The procedure for configuring the `devicemapper` is as follows:

   First, create ThinPool:

   ```bash
   $ sudo pvcreate /dev/sdb1 # /dev/sdb1 for example
   $ sudo vgcreate isulad /dev/sdb
   $ sudo echo y | lvcreate --wipesignatures y -n thinpool isulad -L 200G
   $ sudo echo y | lvcreate --wipesignatures y -n thinpoolmeta isulad -L 20G
   $ sudo lvconvert -y --zero n -c 512K --thinpool isulad/thinpool --poolmetadata isulad/thinpoolmeta
   $ sudo lvchange --metadataprofile isulad-thinpool isulad/thinpool
   ```

   Then,add configuration for `devicemapper` in `/etc/isulad/daemon.json`:

   ```json
   "storage-driver": "devicemapper"
   "storage-opts": [
   		"dm.thinpooldev=/dev/mapper/isulad-thinpool",
   	    "dm.fs=ext4",
   	    "dm.min_free_space=10%"
       ]
   ```

3. Restart `isulad`:

   ```bash
   $ sudo systemctl restart isulad
   ```

4. Create `kata-runtime.yaml`. For example:

   ```yaml
   apiVersion: node.k8s.io/v1beta1
   kind: RuntimeClass
   metadata:
     name: kata-runtime
   handler: kata-runtime
   ```

   Execute `kubectl apply -f kata-runtime.yaml`

5. Create pod spec `kata-pod.yaml`. For example:

   ```yaml
   apiVersion: v1
   kind: Pod
   metadata:
     name: kata-pod-example
   spec:
     runtimeClassName: kata-runtime
     containers:
     - name: kata-pod
       image: busybox:latest
       command: ["/bin/sh"]
       args: ["-c", "sleep 1000"]
   ```

6. Run pod:

   ```bash
   $ kubectl create -f kata-pod.yaml
   $ kubectl get pod
   NAME               READY   STATUS    RESTARTS   AGE
   kata-pod-example   1/1     Running   4          2s
   ```


## CNI Network Configuration

iSulad realize the CRI interface to connect to the CNI network, parse the CNI network configuration files, join or exit CNI network. In this section, we call CRI interface to start pod to verify the CNI network configuration for simplicity.

1. Configure `isulad` in `/etc/isulad/daemon.json` :

   ```json
   "network-plugin": "cni",
   "cni-bin-dir": "/opt/cni/bin",
   "cni-conf-dir": "/etc/cni/net.d",
   ```

2. Prepare CNI network plugins:

   Compile and genetate the CNI plugin binaries, and copy binaries to the directory `/opt/cni/bin`.

   ```bash
   $ git clone https://github.com/containernetworking/plugins.git
   $ cd plugins && ./build_linux.sh
   $ cd ./bin && ls
   bandwidth bridge dhcp firewall flannel ...
   ```

3. Prepare CNI network configuration:

   The conf file suffix can be `.conflist` or  `.conf`, the difference is whether it contains multiple plugins. For example, we create `10-mynet.conflist`  file under directory `/etc/cni/net.d/`, the content is as follows:

   ```json
   {
       "cniVersion": "0.3.1",
       "name": "default",
       "plugins": [
           {
               "name": "default",
               "type": "ptp",
               "ipMasq": true,
               "ipam": {
                   "type": "host-local",
                   "subnet": "10.1.0.0/16",
                   "routes": [
                       {
                           "dst": "0.0.0.0/0"
                       }
                   ]
               }
           },
           {
               "type": "portmap",
               "capabilities": {
                   "portMappings": true
               }
           }
       ]
   }
   ```

4. Configure sandbox-config.json :

   ```json
   {
       "port_mappings":[{"protocol": 1, "container_port": 80, "host_port": 8080}],
       "metadata": {
           "name": "test",
           "namespace": "default",
           "attempt": 1,
           "uid": "hdishd83djaidwnduwk28bcsb"
       },
       "labels": {
   	    "filter_label_key": "filter_label_val" 
       },
       "linux": {
       }
   }
   ```

5. Restart `isulad` and start Pod:

   ```sh
   $ sudo systemctl restart isulad
   $ sudo crictl -i unix:///var/run/isulad.sock -r unix:///var/run/isulad.sock runp sandbox-config.json
   ```

6. View pod network informations:

   ```sh
   $ sudo crictl -i unix:///var/run/isulad.sock -r unix:///var/run/isulad.sock inspectp <pod-id>
   ```