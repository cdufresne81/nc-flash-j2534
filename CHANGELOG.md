# Changelog

The release notes of each version include its section from this file.

## v1.1.0

- **Put the cable on WinUSB without NC Flash or Zadig.** Each zip now has a `driver-setup` folder: run `Install WinUSB driver.cmd` (one administrator prompt), and `Restore Tactrix driver.cmd` to undo it. It is NC Flash's own driver switch, shipped unchanged, so it handles the cable with or without an SD card, and NC Flash and `driver-setup` can each undo the other. The driver itself is unchanged from v1.0.3.

## v1.0.3

- **The cable now works with an SD card in it.** The OpenPort 2.0 with a microSD card inserted enumerates as USB `0403:CC4C` with an extra mass-storage interface; the driver now opens it as well as `0403:CC4D` (no card).
- The J2534 interface is now chosen explicitly: the first CDC data interface (class `0x0A`) with exactly one bulk IN and one bulk OUT endpoint. The old rule (the last interface with two endpoints) would have picked the SD card's mass storage on `0403:CC4C`; v1.0.2 and earlier never opened `0403:CC4C`, so they failed with "Cannot find device" instead. If no such interface exists, `PassThruOpen` fails with `ERR_DEVICE_NOT_CONNECTED` and says why in `PassThruGetLastError`.
- Linux: the kernel driver is detached from the J2534 interface only, so the SD card stays mounted.

## v1.0.2

- `PassThruReadMsgs`: every read of a USB transfer stays within the bytes received; no empty or oversized message is queued; messages completed before a read timeout are reported in `pNumMsgs`.

## v1.0.1

- `PassThruReadMsgs`: a reply is no longer lost when the cable sends a cable-error packet (`are`) in the same USB transfer.

## v1.0.0

- First release: x64 and x86 builds, `__stdcall` exports with undecorated names, static CRT (no Visual C++ runtime needed).
