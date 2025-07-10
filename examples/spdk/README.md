# how to compile
Two choice here:
- `compile dependencies from source codes`, with photon provide cmake, and turn `PHOTON_BUILD_DEPENDENCIES` to `ON`
    - has the chance meeting not found isal, just try install it
- `install dependencies by yourself to system path` (e.g. /usr/local/lib, /usr/local/lib64, /usr/local/include, /usr/lib, /usr/lib64, /usr/include)
    - Attention: keep only one version install in /usr/local/lib, /usr/local/lib64, /usr/lib, /usr/lib64. Or will report warning "may conflict" during compiling, or even some strange "undefined reference" error.
    - If you install dependencies by compile from source codes, it has a big chance installing them to /usr/local. Sometimes, default pkg-config and ldconfig cannot find them, because of /usr/local not include in searching path. You can add /usr/local/xxx/pkgconfig to PKG_CONFIG_PATH (environment variable) for pkg-config and add a file including "/usr/local/xxx" to /etc/ld.so.conf.d/xxx.conf for ldconfig.

## Some explanations about enable fstack_dpdk and spdk at the same time
fstack and spdk are all rely on dpdk, but the method they used to "rely" is very different.
fstack directly include it in the source code, but still need to compile and install it to system path independently.
spdk use dpdk as a submodule, and has some custom compile setting. i.e. when execute spdk's "make", will compile dpdk from source code with custom compile setting.
dpdk not force need isal in default setting, but spdk need it.
dpdk will decide whether to use isal by pkg-config in meson, if pkg-config not find isal, for more details, can check meson.build in dpdk.

Obviously, there exist a dpdk version problem.
So I find a closing dpdk version they use fstack(v1.22)-dpdk(v20.11.6) and spdk(v21.04.x)-dpdk(v20.11.0) for example.
Add a Findxxx.cmake to find dpdk library.

## Tips for compiling spdk
- Download spdk source code in /path/to/spdk
- cd /path/to/spdk
- ./configure --with-shared
- make -j 8
- make install

## Tips for compiling photon with spdk
photon spdk bdev wrapper rely on some spdk dynamic libraries, and these libraries rely on some dpdk libraries and the isal library.
- cd /path/to/photon
- cmake -B build -DPHOTON_ENABLE_SPDK=ON -DPHOTON_BUILD_TESTING=ON
- make -C build -j 8

# how to run
example
``` shell
sudo ./build/examples-output/bdev-example --json ./examples/spdk/bdev.json
sudo ./build/examples-output/nvme-example
```
tests
``` shell
sudo ./build/output/test-spdkbdev
sudo ./build/output/test-spdknvme
```