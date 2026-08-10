# Contributing

Most of these drivers are untested on real hardware. The single most useful contribution is plugging in a printer and telling us what happened.

## Testing a Printer

1. Build and install:
   ```bash
   xcode-select --install   # if you haven't already
   sudo ./install.sh
   ```

2. Connect your printer via USB.

3. Add it in **System Settings > Printers & Scanners** — select the matching PPD.

4. For firmware printers (HP LaserJet 1000/1005/1018/1020/P1005/P1006/P1505), upload firmware first:
   ```bash
   lp -oraw /usr/local/share/foo2zjs/firmware/<your-firmware>.dl
   ```
   HP LaserJet 1018 testers can instead install the tested automatic loader:
   ```bash
   sudo ./install-firmware-loader.sh
   ```

5. Print the included test page:
   ```bash
   lp -d <your_printer_queue> testpage.pdf
   ```

6. [Open a Printer Test Report](../../issues/new?template=printer-test-report.yml) with your results.

Even partial results help — "prints but cuts off the bottom" is more useful than silence.

## Reporting Bugs

If something doesn't work, include the CUPS error log:

```bash
tail -100 /var/log/cups/error_log
```

Use the [Bug Report](../../issues/new?template=bug-report.yml) template.

## Submitting Changes

1. Fork the repo
2. Create a branch: `git checkout -b fix-samsung-clp300-margins`
3. Make your changes
4. Test on your printer if possible
5. Open a PR with:
   - Which printer you tested on (or "untested" if you don't have the hardware)
   - What the change does and why

## Building from Source

```bash
make              # all 9 filters
make rastertozjs  # single filter
make tools        # arm2hpdl, xqxdecode utilities
make clean
```

Requirements: macOS 14+, Apple Silicon, Xcode Command Line Tools. The filters link against CUPS libraries (`-lcups -lcupsimage`) which ship with macOS.
