# hardware-health #

This module contains the STMicroelectronics android.hardware.health binary source code.
The STM32MPU reference devices pretend to be a device with a battery. For that purpose a dummy-battery driver is available.

It is part of the STMicroelectronics delivery for Android.

## Description ##

This module implements android.hardware.health AIDL version 1.
Please see the Android delivery release notes for more details.

## Documentation ##

* The [release notes][] provide information on the release.
[release notes]: https://wiki.st.com/stm32mpu-ecosystem-v5/wiki/STM32_MPU_OpenSTDroid_release_note_-_v5.1.0

## Dependencies ##

This module can't be used alone. It is part of the STMicroelectronics delivery for Android.

```
PRODUCT_PACKAGES += \
    android.hardware.health-service.stm32mpu.emmc \
    android.hardware.health-service.stm32mpu_recovery.emmc
```

## Containing ##

This directory contains the sources and associated Android makefile to generate the health binary.

## License ##

This module is distributed under the Apache License, Version 2.0 found in the [LICENSE](./LICENSE) file.
