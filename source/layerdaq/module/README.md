# build kernel module

## Within CMake project (opt.A)
```bash
cmake [path-to-main-cmake-project]
make adenium-dma-module
```

## make (opt.B)

```bash
make
```

# install kernel module

```bash
make modules_install
```

# load kernel module manully

```bash
insmod alteldma.ko
```
