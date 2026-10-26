# Bundled commands, the icon, and porting

*[← back to the README](../README.en.md)　/　日本語: [tools.md](tools.md)*

Tools for verification and adjustment — none of them are needed for everyday printing — plus what to change when porting to another printer.

## Bundled commands

All of these come with the package and land in `/usr/local/bin`. Everyday printing happens in the
print dialog, so what is here are **tools for verification and adjustment**.

### mi700print — print text as text

Converts UTF-8 text to 201PL. It bypasses CUPS, so there is no rasterizing and the printer's built-in
fonts are used. Fast, and no ruled-line misalignment.

```bash
echo "見積書" | mi700print | lp -d <queue> -o raw
mi700print -w 100 < memo.txt | lp -d <queue> -o raw
```

| | |
|---|---|
| `-w N` | Wrap column (default 80) |
| `-n` | Do not send the trailing FF |

Japanese goes out as `ESC K` + JIS, newlines are CR+LF, and the page ends with FF.
**Unconvertible characters always become the two-byte `〓`**; using a one-byte `?` would shift the
rest of the kanji stream. `～` `－` `∥` are normalized because CP932 and JIS X 0208 assign them
differently, but **`￥` is not, since JIS 216F has it directly**.

### pdf2mi700 — convert PDF/PS without CUPS

The legacy path, usable without installing the driver. Rasterizing needs Ghostscript.

```bash
pdf2mi700 --source rear --quality std-uni doc.pdf > out.prn
lp -d <queue> -o raw out.prn
```

| | |
|---|---|
| `--source` | `feeder` / `guide` / `reartractor` / `fronttractor` |
| `--eject` | `front` / `rear` (cut sheets only) |
| `--quality` | `std-bi` / `std-uni` / `draft-bi` / `draft-uni` / `none` |
| `--form-lines N` | Set the form length in lines (default: computed from the page height) |
| `--bottom N` | Bottom-region lines (the standard 25.4 mm is 6) |
| `--no-form-length` | Do not send a form length; follow the printer's setting |
| `--paper NAME` | Use the initialization sequence of a registered preset |
| `--list` | List the presets |

### mi700decode — turn a .prn back into an image

**Lets you verify output without using paper.** It writes PBM to stdout, which `sips` can turn into
PNG.

```bash
mi700decode out.prn > out.pbm
sips -s format png out.pbm --out out.png
```

This is also how the coordinate system was proven to match the Windows driver: decode a capture that
printed correctly on real hardware, push the image back through this driver, and compare where the
dots land.

### mi700align — measure the print position

A 10 mm ruler pattern that measures "how many mm from the paper edge the page origin is" with a
single sheet. **Re-measure whenever you move the tractor.**

**The scale is in page coordinates** (mm from the page origin) and the reference lines are drawn at
**the corner of the imageable area**. No line is drawn at the page origin itself: the origin is often
outside the imageable area (1.6 mm left and 8.7 mm top for cut sheets), and a line there would
disappear during rasterizing.

Reading it is easy: **measure from the paper edge to any tick and subtract that tick's number** to get
the page origin. The sheet itself carries the same explanation plus the imageable-area corner in page
coordinates.

```bash
mi700align                       # list the registered paper sizes
mi700align Cont10x11 > a.ps      # by paper name
mi700align --size 210x297 > a.ps # by actual size (mm)
```

It emits PostScript, so convert it to PDF before handing it to `lp` if you print through CUPS.

> **Trap:** do not name the file you pass to Ghostscript `align.ps`. gs searches its own library
> first and **picks up its own `align.ps` instead**.

### mi700diff — compare captures instruction by instruction

```bash
mi700diff a.prn b.prn
```

Give it two captures taken on Windows **with exactly one setting changed**, and the control codes for
that setting fall out as the difference. The paper-source and eject codes were found this way.

### mi700preset — register a capture as a preset

```bash
mi700preset capture.prn rear     # register
mi700preset --list               # list
```

Once registered, `pdf2mi700 --paper <name>` reuses that initialization sequence.

> You can capture `.prn` files on Windows by changing the printer port to `FILE:` and printing.
> **They contain exactly what you printed, so check them with `mi700decode` before publishing them.**

## The icon

An icon is bundled for the print dialog, Printers & Scanners and the iPhone printer list. It is a
stylized drawing of the real machine (the horizontal 700 series), drawn by
[`tools/make_icon.py`](../tools/make_icon.py).

| Path | Mechanism | Location |
|---|---|---|
| PPD | `*APPrinterIconPath` | `/usr/local/share/mi700/MI700XX.icns` |
| PAPPL version | `printer-icons` (48 / 128 / 512 PNG) | `/usr/local/share/mi700/icons/` |

Redrawing it needs Pillow:

```bash
python3 tools/make_icon.py --all driver/icons   # regenerate the three PNGs and the .icns
python3 tools/make_icon.py icon.png             # just a 512px preview
```

## Signing and notarization (for redistribution)

`build-pkg.sh` **signs automatically if a Developer ID certificate is present**. Without one it just
produces an unsigned package; the build does not fail. For your own use no signature is needed.

To notarize as well, store the `notarytool` credentials in the keychain once:

```bash
xcrun notarytool store-credentials mi700 --apple-id <Apple ID> --team-id <Team ID>
MI700_NOTARY=mi700 ./driver/build-pkg.sh 4.0
```

That signs the filter with the Hardened Runtime, signs the pkg, notarizes it and runs
`stapler staple`. To pick certificates explicitly, use `MI700_SIGN_APP` / `MI700_SIGN_PKG`.

Note that **a package you built from a `git clone` is not blocked by Gatekeeper even unsigned**,
because the quarantine attribute is only attached to files downloaded with a browser.

`mi700setup` creates a `socket://<IP>:9100` queue.

## Porting to other printers

Any 201PL-family printer stands a good chance. Here is what actually matters, and
**how to make progress without wasting paper**.

### 1. Confirm the printer language

```bash
snmpget -v1 -c public <IP> 1.3.6.1.2.1.25.3.2.1.3.1   # model name
```

The LAN board's web UI may show it too (mine printed `201PL` at `/opt.html`). The printer's own
settings printout works as well. ESC/P or NPDL are different beasts and this driver will not work.

### 2. Capture from Windows

**This is the single best source of information.** Change the printer port to `FILE:` and printing
produces a `.prn`.

**Take two captures with exactly one setting changed** and run `mi700diff` on them; the control code
for that setting falls out as the difference. Every paper-source, eject-direction and print-quality
code was found this way.

### 3. Verify without paper

Turn a capture back into an image with `mi700decode`, **push that image through your own driver**, and
compare whether the same dots land at the same coordinates. That proves the coordinate systems match
without using a single sheet. Printing for real can wait until after that.

### 4. What to change

| File | What |
|---|---|
| `driver/mkppd.py` | `*Resolution` (dpi), `*ModelName` / `*Product`, the `InputSlot` choices |
| `driver/rastertomi700.c` | `BAND` (number of pins), `TDPI` (the `ESC T` unit), the init sequence, the source and eject codes |
| `forms.conf` | Paper dimensions and **the imageable area** |
| `driver/mi700setup` | The default queue name |

`*ModelName` and `*Product` **drive automatic driver selection**. Match them to the string SNMP
`hrDeviceDescr` returns, and macOS picks the driver as soon as you type an IP in "Add Printer".

### Easy mistakes

In the order I actually made them:

- **Resolution.** Horizontally it is one dot of `ESC F`, vertically one dot inside an `ESC J` band.
  The 700XX is 160 dpi both ways, but 24-pin machines are sometimes 180 dpi. Check by whether
  full-width characters come out square, or by printing a rectangle of known size and measuring.
  **This driver assumes square pixels** (horizontally, one CUPS raster pixel = one dot). A machine with
  different horizontal and vertical resolutions needs changes in the filter
- **Bit order inside a band.** The 700XX has the LSB at the top. Reversed, every band is flipped
  vertically. The quick route is to try all four bit orders in `mi700decode` and pick the readable one
- **The `ESC T` unit.** It is 1/120 inch. Mistaking it for 1/180 stretches everything vertically by 1.5×
- **The imageable area.** Setting it to the whole sheet **stops the printer with an error**. Calculate
  it from the **recommended print area** in the vendor's paper design guide; the maximum print area
  comes with warnings about paper jams and head failure
- **9-pin machines.** `ESC J` assumes 24 dots, so an 8-dot machine needs the bit-image assembly
  rewritten

## Layout

```
driver/rastertomi700.c   CUPS raster -> 201PL filter (C, no dependencies)
driver/mi700enc.c        201PL assembly (shared by the filter and the PAPPL version)
driver/mkppd.py          generates the Shift-JIS PPD from forms.conf
driver/build-pkg.sh      builds the distribution package
driver/mkippattr.py      generates the ippeveprinter attribute file from forms.conf
driver/mi700ipp          registers ippeveprinter with launchd (the AirPrint route)
driver/mi700pappl.c      the PAPPL Printer Application
driver/build-pappl.sh    builds the PAPPL version
driver/mi700app          registers the PAPPL version with launchd and creates queues
contrib/mi700ippcmd      the conversion command ippeveprinter calls
driver/mi700setup        registers a queue
driver/mi700default      sets queue defaults
driver/mi700status       reads printer status over SNMP
driver/icons/            the printer icon (three PNGs and an .icns)
docs/                    the detailed documents
forms.conf               paper definitions
tools/mi700print         prints UTF-8 text with ESC K + JIS
tools/pdf2mi700          converts PDF/PS through Ghostscript (the legacy, non-CUPS route)
tools/mi700decode        turns a .prn back into PBM (verification without paper)
tools/mi700align         builds the print-position test pattern
tools/mi700diff          compares two captures
tools/mi700preset        registers a capture as a preset
tools/make_icon.py       draws the icon (needs Pillow)
```

How to use `tools/` is in [Bundled commands](#bundled-commands).

The PPD is written with `*LanguageEncoding: JIS83-RKSJ`, i.e. **Shift-JIS**. UTF-8 makes the print
dialog show mojibake.
