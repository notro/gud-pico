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

The default VID:PID won't be supported in the host driver before Linux v5.15 is out. Can be changed in `libraries/gud_pico/tusb_config.h`.

TODO:
- See if it's possible to use unlz4. It works with the test image in ```modetest``` but hangs when flipping ```modetest -v```. Decompression doesn't seem to be a bottleneck, so maybe drop this?
