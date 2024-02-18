# libdmdutil

A cross platform library for performing DMD tasks.

This library is currently used by [Visual Pinball Standalone](https://github.com/vpinball/vpinball/tree/standalone) for processing [PinMAME](https://github.com/vpinball/pinmame/tree/master/src/libpinmame) and [FlexDMD](https://github.com/vbousquet/flexdmd) DMD frames. It supports colorizing using [Serum](https://github.com/zesinger/libserum), outputing to [ZeDMD](https://github.com/ppuc/zedmd) and [Pixelcade](https://pixelcade.org) devices, and providing intensity and RGB24 buffers that can be used to render in table and external DMDs for [Visual Pinball](https://github.com/vpinball/vpinball).

## Usage:

```
#include "DMDUtil/DMDUtil.h"
.
.
void setup()
{
   DMDUtil::Config* pConfig = DMDUtil::Config::GetInstance();
   pConfig->SetZeDMD(true);
   pConfig->SetZeDMDDevice("/dev/cu.usbserial-0001");
   pConfig->SetPixelcadeDMD(false);
}

void test()
{
   DMDUtil::DMD* pDmd = new DMDUtil::DMD();
   pDmd->FindDisplays();

   DMDUtil::RGB24DMD* pRGB24DMD = pDmd->CreateRGB24DMD(128, 32);

   uint8_t* pData = (uint8_t*)malloc(128 * 32 * 3);
   .
   .
   .
   pDmd->UpdateRGB24Data((const UINT8*)pData, 128, 32);

   uint8_t* pRGB24Data = pRGB24DMD->GetRGB24Data();

   if (pRGB24Data) {
      // Render pRGB24Data
   }

   pDmd->DestroyRGB24DMD(pRGB24DMD);
}
```

## Building:

#### Windows (x64)

```shell
platforms/win/x64/external.sh
cmake -G "Visual Studio 17 2022" -DPLATFORM=win -DARCH=x64 -B build
cmake --build build --config Release
```

#### Windows (x86)

```shell
platforms/win/x86/external.sh
cmake -G "Visual Studio 17 2022" -A Win32 -DPLATFORM=win -DARCH=x86 -B build
cmake --build build --config Release
```

#### Linux (x64)
```shell
platforms/linux/x64/external.sh
cmake -DPLATFORM=linux -DARCH=x64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### Linux (aarch64)
```shell
platforms/linux/aarch64/external.sh
cmake -DPLATFORM=linux -DARCH=aarch64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### MacOS (arm64)
```shell
platforms/macos/arm64/external.sh
cmake -DPLATFORM=macos -DARCH=arm64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### MacOS (x64)
```shell
platforms/macos/x64/external.sh
cmake -DPLATFORM=macos -DARCH=x64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### iOS (arm64)
```shell
platforms/ios/arm64/external.sh
cmake -DPLATFORM=ios -DARCH=arm64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### tvOS (arm64)
```shell
platforms/tvos/arm64/external.sh
cmake -DPLATFORM=tvos -DARCH=arm64 -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```

#### Android (arm64-v8a)
```shell
platforms/android/arm64-v8a/external.sh
cmake -DPLATFORM=android -DARCH=arm64-v8a -DCMAKE_BUILD_TYPE=Release -B build
cmake --build build
```
