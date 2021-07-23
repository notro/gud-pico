Raspberry Pi Pico GUD USB Display
---------------------------------

GUD implementation for the Raspberry Pi Pico with a [Pimoroni Pico Display](https://shop.pimoroni.com/products/pico-display-pack).

The ```PICO_SDK_PATH``` env var should point to the Pico SDK.

Build
```
$ git clone https://github.com/notro/gud-pico
$ cd gud-pico
$ mkdir build && cd build
$ cmake ..
$ make

```

The default VID:PID won't be supported in the host driver before Linux v5.15 is out (it's present in the rPi 5.10 backport). Can be changed in `libraries/gud_pico/tusb_config.h`.
