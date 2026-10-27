# nc-flash-j2534

An open-source SAE J2534-1 (PassThru API 04.04) driver for the
[Tactrix OpenPort 2.0](https://www.tactrix.com/) cable and its clones. It is
the driver [NC Flash](https://github.com/cdufresne81/nc-flash) uses to talk to
the cable, and it is published here so other J2534 programs (e.g. RomDrop) can
use it too.

It is based on [NikolaKozina/j2534](https://github.com/NikolaKozina/j2534) by
NikolaKozina and Dale Schultz (BSD-3-Clause), with fixes made for NC Flash
(long ISO 15765 reads, one open per process, bounded USB writes, CAN ID checks,
a debug log, `__stdcall` exports for 32-bit hosts).

## Why it exists

Tactrix's own driver is a kernel driver (`openport.sys`) plus `op20pt32.dll`.
Since 2026, Windows 11 refuses to load `openport.sys` (Device Manager shows the
cable with **Code 39**), so the cable stops working in every program that uses
Tactrix's DLL.

This driver does not need a kernel driver of its own. The cable is bound to
Microsoft's built-in **WinUSB** driver, and `j2534.dll` talks to it through
[libusb](https://libusb.info/).

## Download

Get the zip for your program's bitness from
[Releases](https://github.com/cdufresne81/nc-flash-j2534/releases):

| Zip | For | Status |
|---|---|---|
| `nc-flash-j2534-<ver>-x64.zip` | 64-bit programs (NC Flash) | Used by NC Flash. |
| `nc-flash-j2534-<ver>-x86.zip` | 32-bit programs (RomDrop) | **Experimental: not yet flash-tested with RomDrop.** |

Each zip contains `j2534.dll`, `libusb-1.0.dll`, the two licences,
`SOURCES.txt` (the exact commits and the CI run that built it) and
`SHA256SUMS.txt`. The DLLs need no Visual C++ runtime. Everything is built from
source in GitHub Actions ([release.yml](.github/workflows/release.yml)).

Nothing gets installed system-wide. Put the two DLLs in the folder of the
program that loads them, and don't copy them into `C:\Windows\System32` or
`SysWOW64`. Tactrix's `op20pt32.dll` lives there, and EcuFlash depends on it.

## 1. Put the cable on WinUSB

Unplug any other OpenPort programs first, and remove the SD card from the cable.

- **With NC Flash:** Settings > ECU > J2534 > Tactrix OpenPort Driver.
- **Manually:** run [Zadig](https://zadig.akeo.ie/), Options > List All Devices,
  pick the OpenPort 2.0 (USB ID `0403` `CC4D`), choose **WinUSB** as the target
  driver and click Replace Driver / Install Driver.

### Reverting to the Tactrix driver

Device Manager > Universal Serial Bus devices > the OpenPort > Update driver >
Browse my computer for drivers > Let me pick from a list of available drivers >
**Tactrix Openport 2.0 J2534 Vehicle Interface** > Next. On Windows 11 builds
that block `openport.sys`, the Tactrix driver will show Code 39 again.

## 2. Debug log

Set the environment variable `J2534_OP2_LOG` to a file path before starting the
program, and the driver appends a log of every call and USB transfer to it:

```bat
set J2534_OP2_LOG=C:\temp\j2534.log
romdrop.exe
```

Leave it unset for normal use.

## 3. Using it with RomDrop (experimental)

> [!WARNING]
> The x86 build has not yet completed a RomDrop read or flash. A failed flash
> can brick an ECU. Only try it if you have a recovery path (e.g. NC Flash x64
> on the same WinUSB cable), and run with `J2534_OP2_LOG` set.

RomDrop is a 32-bit program. It loads `op20pt32.dll` by name, so a copy in its
own folder is used instead of the Tactrix one in `SysWOW64`. RomDrop never
reads the registry.

1. Put the cable on WinUSB (step 1).
2. From `nc-flash-j2534-<ver>-x86.zip`, copy into the folder that contains
   `romdrop.exe`:
   - `j2534.dll`, **renamed to `op20pt32.dll`**
   - `libusb-1.0.dll`
3. Start RomDrop with the cable plugged in.
   - **It worked** if you get the full menu (C, D, F, P, R, S, V).
   - **The open failed** if you only get P, V, Q. Check the cable is on WinUSB,
     no other program has it open, then look at the `J2534_OP2_LOG` log.

Only one program can use the cable at a time. A second program gets
`ERR_DEVICE_IN_USE` until the first one closes the cable.

**Uninstall:** delete `op20pt32.dll` and `libusb-1.0.dll` from RomDrop's
folder, and revert the cable to the Tactrix driver if you want the original
setup back.

## Building

Releases are built by [release.yml](.github/workflows/release.yml): libusb
v1.0.30 and this driver for x64 and x86 with Visual Studio 2022 (v143), static
CRT. The workflow checks that the DLLs export exactly the 14 `PassThru*`
functions with undecorated names, and that they load from Python.

To build locally on Windows:

- Build libusb (`msvc\libusb_dll.vcxproj`, configuration `Release-MT`), or
  download the libusb Windows binaries.
- Copy `libusb.h` into `libusb\include\libusb-1.0\` in this repository, and
  `libusb-1.0.lib` + `libusb-1.0.dll` into `libusb\MS64\Release\dll\` (x64)
  or `libusb\MS32\Release\dll\` (x86).
- From a "Native Tools Command Prompt for VS":
  `msbuild j2534\j2534.vcxproj /p:Configuration=Release /p:Platform=x64`
  (or `/p:Platform=Win32`).

On Linux, install `pkg-config` and libusb 1.0 development headers, then run
`make` in `j2534/`. Give your user access to the cable with a udev rule such as:

```
SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTR{idProduct}=="cc4d", GROUP="dialout", MODE="0666"
```

## Licences

- `j2534.dll`: BSD-3-Clause, see [LICENSE](LICENSE).
- `libusb-1.0.dll`: LGPL-2.1. Each release ships libusb's `COPYING` and attaches
  the exact libusb source it was built from. You may replace `libusb-1.0.dll`
  with your own libusb build.
