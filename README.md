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
| `nc-flash-j2534-<ver>-x86.zip` | 32-bit programs (RomDrop) | Tested with RomDrop: read and full flash. |

Each zip contains `j2534.dll`, `libusb-1.0.dll`, the `driver-setup` folder
(step 1), the two licences, `SOURCES.txt` (the exact commits and the CI run that
built it) and `SHA256SUMS.txt`. The DLLs need no Visual C++ runtime. Everything
is built from source in GitHub Actions
([release.yml](.github/workflows/release.yml)).

The DLLs aren't installed system-wide. Put them in the folder of the program
that loads them, and don't copy them into `C:\Windows\System32` or `SysWOW64`.
Tactrix's `op20pt32.dll` lives there, and EcuFlash depends on it.

## 1. Put the cable on WinUSB

Close every program that uses the cable first.

The cable can keep its microSD card. Without a card it shows up as USB ID
`0403` `CC4D`; with one, as `0403` `CC4C`, which adds the card as a USB drive.
The driver only ever uses the cable's J2534 interface (a CDC data interface,
interface 1), never the card's mass-storage interface. Windows sees the two
layouts as different devices, so bind WinUSB in the layout you use: if you
switch between card and no card, do step 1 for each.

- **Without NC Flash:** extract the whole zip (either one), open its
  `driver-setup` folder and run **`Install WinUSB driver.cmd`**. Windows asks
  for administrator rights once; the window then shows what was done. It
  handles the cable with or without an SD card, and leaves the card working
  as a USB drive.
- **With NC Flash:** Settings > ECU > J2534 > Tactrix OpenPort Driver. With an
  SD card in the cable, this needs an NC Flash version that supports
  `0403` `CC4C`; otherwise use `driver-setup`.
- **With Zadig:** run [Zadig](https://zadig.akeo.ie/), Options > List All Devices,
  pick the OpenPort 2.0, choose **WinUSB** as the target driver and click
  Replace Driver / Install Driver. Pick the entry with USB ID `0403` `CC4D`, or
  with a card, the one with USB ID `0403` `CC4C` and **Interface 0** (the
  cable's J2534 part, interfaces 0 and 1). Leave the card's entry (Interface 2,
  USB Mass Storage) as it is.

`driver-setup` is the same switch NC Flash runs, shipped unchanged from
[NC Flash](https://github.com/cdufresne81/nc-flash/blob/master/packaging/openport/openport_winusb.ps1)
(`SOURCES.txt` names the commit), so either one can undo the other. What it
does:

- Windows only binds WinUSB to a device through a signed setup file, and
  Microsoft's own one doesn't list this cable. So the switch writes a small
  one (`ncflash_openport.inf`) that reuses Windows' `winusb.inf`, and signs
  it with a certificate made on your PC for this alone. The certificate's
  private key is deleted right after signing, before Windows is told to
  trust it, so nothing else can ever be signed with it. Zadig does much the
  same.
- The cable then shows in Device Manager under **Universal Serial Bus
  devices**, with driver provider NC Flash.
- It logs to `C:\ProgramData\NC Flash\OpenPort\`.

### Reverting to the Tactrix driver

Run **`driver-setup\Restore Tactrix driver.cmd`**. It removes the WinUSB setup
(whether made by `driver-setup`, NC Flash or Zadig) and its certificate, and
the cable goes back to Tactrix's driver. On Windows 11 builds that block
`openport.sys`, the Tactrix driver will show Code 39 again.

## 2. Debug log

Set the environment variable `J2534_OP2_LOG` to a file path before starting the
program. From `PassThruOpen` on, the driver appends every J2534 call and USB
transfer to that file:

```bat
set J2534_OP2_LOG=C:\temp\j2534.log
romdrop.exe
```

Leave it unset (or set it to `0`) for normal use. The log grows quickly during a
flash.

## 3. Using it with RomDrop

Tested with RomDrop rev 21053000 on a Tactrix OpenPort 2.0 clone and an MX-5 NC
ECU: DTCs, ROM read (216 s, byte-identical to other tools' reads of the same
ECU) and full flash (52 s, read back and verified).

> [!WARNING]
> A failed flash can brick an ECU. Keep a recovery path (e.g. NC Flash on the
> same WinUSB cable) when you flash.

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

RomDrop's DTC list can show wrong codes after the first one (e.g. `P07FF`,
`UFF12`). That is RomDrop's own decoding: the ECU's reply reaches it intact.

Only one program can use the cable at a time. While NC Flash or another
program using this driver has it open, RomDrop's open fails (short menu) until that
program closes the cable. Within one program, a second `PassThruOpen` returns
`ERR_DEVICE_IN_USE`.

**Uninstall:** delete `op20pt32.dll` and `libusb-1.0.dll` from RomDrop's
folder, and run `Restore Tactrix driver.cmd` if you want the original setup
back.

## Building

Releases are built by [release.yml](.github/workflows/release.yml): libusb
v1.0.30 and this driver for x64 and x86 with Visual Studio 2022 (v143), static
CRT. The workflow checks that the DLLs export exactly the 14 `PassThru*`
functions with undecorated names, and that they load from Python. Unit tests in
`tests/` run on Linux against a stubbed libusb (with AddressSanitizer). Changes
per version are in [CHANGELOG.md](CHANGELOG.md).

To build locally on Windows:

- Build libusb v1.0.30 (`msvc\libusb_dll.vcxproj`, configuration `Release-MT`;
  the output lands in `build\v143\<platform>\Release-MT\dll\`), or download
  the libusb Windows binaries.
- Copy `libusb.h` into `libusb\include\libusb-1.0\` in this repository, and
  `libusb-1.0.lib` + `libusb-1.0.dll` into `libusb\MS64\Release\dll\` (x64)
  or `libusb\MS32\Release\dll\` (x86).
- From a "Developer Command Prompt for VS 2022":
  `msbuild j2534\j2534.vcxproj /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143`
  (or `/p:Platform=Win32`). The project file still names the VS 2019 toolset
  (v142), hence the override.

On Linux, install `pkg-config` and libusb 1.0 development headers, then run
`make` in `j2534/`. Give your user access to the cable with a udev rule such as:

```
SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTR{idProduct}=="cc4d", GROUP="dialout", MODE="0666"
SUBSYSTEM=="usb", ATTRS{idVendor}=="0403", ATTR{idProduct}=="cc4c", GROUP="dialout", MODE="0666"
```

The first line is the cable without an SD card, the second with one. On open,
the driver detaches `cdc_acm` from the J2534 interface only; the card stays
mounted.

### Registry files in `extras/`

`extras/j2534_x86.reg` and `extras/j2534_x64.reg` come from upstream. They
register the DLL under `HKLM\SOFTWARE\...\PassThruSupport.04.04` with the DLL
in `System32`/`SysWOW64`, for programs that pick their J2534 driver from the
registry. The releases don't use them, and you don't need them for NC Flash or
RomDrop. Don't merge them on a PC where EcuFlash relies on the Tactrix entry.

## Licences

- `j2534.dll`: BSD-3-Clause, see [LICENSE](LICENSE).
- `libusb-1.0.dll`: LGPL-2.1. Each release ships libusb's `COPYING` and attaches
  the exact libusb source it was built from. You may replace `libusb-1.0.dll`
  with your own libusb build.
