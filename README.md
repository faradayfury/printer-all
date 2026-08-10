# printer-all — ARM64 macOS CUPS Drivers for 89 Printers

Native Apple Silicon CUPS raster filters for printers that have no official macOS ARM64 driver. No Ghostscript, no Rosetta — uses macOS's built-in `cgpdftoraster`.

```
PDF → cgpdftoraster (macOS built-in) → rastertoXXX (our filter) → printer
```

> **Testing status:** Only the HP LaserJet P1007 has been verified on real hardware. The other 88 printers compile and should work — the filters are faithful rewrites of the battle-tested [foo2zjs](http://foo2zjs.rkkda.com/) project — but none have been tested yet. If you have one of these printers, [we'd love your help](#help-us-test).

## Supported Printers

9 format families covering 89 printer models.

### XQX — `rastertoxqx`

| Printer | Status |
|---------|--------|
| HP LaserJet P1005 | Untested |
| HP LaserJet P1006 | Untested |
| HP LaserJet P1007 | **Verified** |
| HP LaserJet P1008 | Untested |

### ZjStream — `rastertozjs`

| Printer | Status |
|---------|--------|
| HP LaserJet 1000 | Untested |
| HP LaserJet 1005 | Untested |
| HP LaserJet 1018 | Untested |
| HP LaserJet 1020 | Untested |
| HP LaserJet 1022 | Untested |
| HP LaserJet 1022n | Untested |
| HP LaserJet 1022nw | Untested |
| HP LaserJet M1005 MFP | Untested |
| HP LaserJet M1120 MFP | Untested |
| HP LaserJet M1319 MFP | Untested |
| HP LaserJet P2014 | Untested |
| HP LaserJet P2014n | Untested |
| HP LaserJet P2035 | Untested |
| HP LaserJet P2035n | Untested |
| HP LaserJet Pro M1212nf MFP | Untested |
| HP LaserJet Pro P1102 | Untested |
| HP LaserJet Pro P1102w | Untested |
| HP LaserJet Pro P1566 | Untested |
| HP LaserJet Pro P1606dn | Untested |
| Minolta magicolor 2200 DL | Untested |
| Minolta magicolor 2300 DL | Untested |
| Minolta magicolor 2430 DL | Untested |

### ZJS (HP 2600n) — `rastertohp`

| Printer | Status |
|---------|--------|
| HP Color LaserJet 1600 | Untested |
| HP Color LaserJet 2600n | Untested |
| HP Color LaserJet CP1215 | Untested |

### HiPerC — `rastertohiperc`

| Printer | Status |
|---------|--------|
| Oki C110 | Untested |
| Oki C301dn | Untested |
| Oki C310dn | Untested |
| Oki C3100 | Untested |
| Oki C3200 | Untested |
| Oki C3300 | Untested |
| Oki C3400 | Untested |
| Oki C3530 MFP | Untested |
| Oki C5100 | Untested |
| Oki C511dn | Untested |
| Oki C5200 | Untested |
| Oki C5500 | Untested |
| Oki C5600 | Untested |
| Oki C5650 | Untested |
| Oki C5800 | Untested |
| Oki C810 | Untested |

### QPDL — `rastertoqpdl`

| Printer | Status |
|---------|--------|
| Samsung CLP-300 | Untested |
| Samsung CLP-310 | Untested |
| Samsung CLP-315 | Untested |
| Samsung CLP-325 | Untested |
| Samsung CLP-365 | Untested |
| Samsung CLP-600 | Untested |
| Samsung CLP-610 | Untested |
| Samsung CLP-620 | Untested |
| Samsung CLX-2160 | Untested |
| Samsung CLX-3160 | Untested |
| Samsung CLX-3175 | Untested |
| Samsung CLX-3185 | Untested |
| Xerox Phaser 6110 | Untested |

### LAVAFLOW — `rastertolava`

| Printer | Status |
|---------|--------|
| Konica Minolta magicolor 1600W | Untested |
| Konica Minolta magicolor 1680MF | Untested |
| Konica Minolta magicolor 1690MF | Untested |
| Konica Minolta magicolor 2430 DL | Untested |
| Konica Minolta magicolor 2480 MF | Untested |
| Konica Minolta magicolor 2490 MF | Untested |
| Konica Minolta magicolor 2530 DL | Untested |
| Konica Minolta magicolor 4690MF | Untested |
| Minolta Color PageWorks Pro L | Untested |
| Olivetti d-Color P160W | Untested |
| Xerox Phaser 6115MFP | Untested |
| Xerox Phaser 6121MFP | Untested |
| Oki C110 | Untested |

### HBPL2 — `rastertohbpl2`

| Printer | Status |
|---------|--------|
| Dell 1355 | Untested |
| Dell C1765 | Untested |
| Epson AcuLaser M1400 | Untested |
| Epson AcuLaser CX17NF | Untested |
| Fuji Xerox DocuPrint CM205 | Untested |
| Fuji Xerox DocuPrint CM215 | Untested |
| Fuji Xerox DocuPrint M215 | Untested |
| Fuji Xerox DocuPrint P205 | Untested |
| Xerox WorkCentre 3045 | Untested |
| Xerox WorkCentre 6015 | Untested |

### OAKT — `rastertooak`

| Printer | Status |
|---------|--------|
| HP Color LaserJet 1500 | Untested |
| Kyocera KM-1635 | Untested |
| Kyocera KM-2035 | Untested |

### SLX — `rastertoslx`

| Printer | Status |
|---------|--------|
| Lexmark C500 | Untested |

## Help Us Test

If you have any of the printers listed above, you can help by testing the driver and reporting results:

1. Install the driver: `sudo ./install.sh`
2. Connect your printer and try printing the test page
3. [Open a Printer Test Report](../../issues/new?template=printer-test-report.yml) with your results — working, partial, or broken

Even a "it prints but the margins are off" report is valuable. Every confirmed printer gets marked as verified in the table above.

## Installation

### Prerequisites

- macOS 14+ on Apple Silicon (arm64)
- Xcode Command Line Tools: `xcode-select --install`

### Quick Install

```bash
sudo ./install.sh
```

This compiles all 9 filters, installs them to `/usr/libexec/cups/filter/`, copies PPDs and firmware, then restarts CUPS.

To install the tested HP LaserJet 1018 firmware autoloader at the same time,
explicitly opt in:

```bash
sudo ./install.sh --firmware-autoload
```

The default install remains unchanged and does not install a system daemon.

### Manual Build

```bash
make          # build all 9 filters
make tools    # build arm2hpdl and xqxdecode utilities
make clean
```

Individual filters:

```bash
make rastertozjs
make rastertohiperc
# etc.
```

### After Installation

1. Connect your printer via USB
2. Add it in **System Settings > Printers & Scanners**
3. Select the matching PPD as the driver

### Firmware Printers

Some HP LaserJets have no onboard firmware and require upload every power cycle:

| Firmware file | Printers |
|---|---|
| `sihp1000.dl` | LaserJet 1000 |
| `sihp1005.dl` | LaserJet 1005 |
| `sihp1018.dl` | LaserJet 1018 |
| `sihp1020.dl` | LaserJet 1020, 1022 series |
| `sihpP1005.dl` | LaserJet P1005, P1006, P1007, P1008 |
| `sihpP1006.dl` | LaserJet P1006 (alternate) |
| `sihpP1505.dl` | LaserJet P1505 series |

Upload firmware after the printer powers on:

```bash
lp -oraw /usr/local/share/foo2zjs/firmware/sihpP1005.dl
```

### Automatic Firmware Loading (HP LaserJet 1018)

The HP LaserJet 1018 firmware loader is a separate, optional component. It
does not install or replace CUPS filters, PPDs, queues, or printer drivers.

Currently supported:

- HP LaserJet 1018 (`03f0:4117`)

The standalone modes and LaunchDaemon have been tested on real HP LaserJet
1018 hardware. Testing covers startup detection, power-cycle removal and
arrival events, conditional upload, post-upload verification, skipping an
already-loaded device, and normal printing afterward. The remaining failure
and sleep/wake cases in the test plan below should be completed before support
is expanded to other models.

The native helper uses IOKit USB arrival notifications and checks the
printer's IEEE-1284 Device ID for `FWVER` before uploading firmware. It does
not poll USB devices and does not upload firmware when it is already loaded.

Build and inspect the current state before installing the daemon:

```bash
make foo2zjs-firmware-loader
./foo2zjs-firmware-loader --status
sudo ./foo2zjs-firmware-loader --once
./foo2zjs-firmware-loader --status
```

Install the system LaunchDaemon:

```bash
sudo ./install-firmware-loader.sh
```

It can also be selected as part of the main installation:

```bash
sudo ./install.sh --firmware-autoload
```

The optional installer only installs the firmware helper, its LaunchDaemon,
and `sihp1018.dl` when that firmware file is missing. In particular, it does
not run `install.sh` and does not touch `rastertozjs` or any installed PPD.

Installed paths:

```text
/usr/local/libexec/foo2zjs/foo2zjs-firmware-loader
/Library/LaunchDaemons/org.foo2zjs.firmware-loader.plist
/usr/local/share/foo2zjs/firmware/sihp1018.dl
```

Inspect the service and log:

```bash
sudo launchctl print system/org.foo2zjs.firmware-loader
sudo tail -f /var/log/foo2zjs-firmware-loader.log
```

Uninstall the helper and LaunchDaemon:

```bash
sudo ./uninstall-firmware-loader.sh
```

Uninstallation leaves firmware files, CUPS queues, filters, and PPDs
unchanged.

#### Firmware Loader Test Plan

1. With the Mac running, power the printer off and on; verify one upload.
2. Start the daemon while the printer is already on; verify startup detection.
3. Restart the daemon after firmware is loaded; verify that upload is skipped.
4. Power-cycle the printer; verify that firmware is uploaded again.
5. Disconnect and reconnect USB; verify arrival/removal logging.
6. Temporarily move the firmware file; verify a clear error without a crash.
7. Restore firmware and print `testpage.pdf` normally.
8. Print a document and verify that firmware is not resent during the job.
9. Sleep and wake the Mac, then verify printing and firmware state.
10. Verify the helper remains native arm64 with system-only dependencies.

## Files

| Path | Purpose |
|------|---------|
| `rastertoXXX.c` | CUPS raster filters (9 files, one per format family) |
| `foo2zjs/jbig.c` | JBIG compression library |
| `foo2zjs/*.h` | Format definitions (xqx.h, zjs.h, hiperc.h, qpdl.h, hbpl.h, oak.h, slx.h) |
| `foo2zjs/*.img` | Firmware images for HP LaserJets |
| `foo2zjs-firmware-loader.c` | Native macOS HP firmware loader |
| `macos/*.plist` | Optional macOS LaunchDaemon definitions |
| `foo2zjs/*.icm` | ICC color profiles for color printers |
| `PPD/` | Printer description files (89 files) |
| `install.sh` | Automated installer |
| `install-firmware-loader.sh` | Installs only the optional firmware loader |
| `uninstall-firmware-loader.sh` | Removes only the optional firmware loader |
| `Makefile` | Build system |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for how to test, report, and submit changes.

## License

GNU General Public License v2 or later. See [LICENSE](LICENSE).

Based on the [foo2zjs](http://foo2zjs.rkkda.com/) project by Rick Richardson (GPL v2+). Firmware images are copyright HP.
