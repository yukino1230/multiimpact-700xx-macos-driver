# What I found out

*[← back to the README](../README.en.md)　/　日本語: [findings.md](findings.md)*

A record of what was established with the real printer and the manuals.
**You do not need any of this to use the driver.** It is here for anyone doing the same thing, and
for my future self.

## Are there alternatives?

As far as I could find, **there was no implementation that works as a macOS printer end to end.**
The closest is Ghostscript's `pr201` output device, which is also listed on OpenPrinting.

I ran it with Ghostscript 10.07 and looked at the output. It builds pages with `ESC F` (horizontal
position) and `ESC J` (24-dot bit image) — **exactly the same structure as this driver**. Even
`ESC T18` = 18/120 inch = exactly 24 dots at 160 dpi matches. Two implementations written
independently arriving at the same answer is **also a cross-check of my analysis.**

But the `pr201` device only emits the bit image. It has none of this:

- Paper source selection, cut-sheet eject direction, form length, print quality
- **Control of the imageable area** (exceeding it stops the printer with an error)
- Integration with the macOS print dialog (no PPD and no queue, so wiring it into CUPS is on you)
- And it keeps the dependency on Ghostscript

In other words, "convert a PDF into PR201 dots" already existed; **what was missing was everything
that makes it a macOS printer.** This driver fills that gap.

## Can macOS handle continuous forms?

You sometimes read that "the macOS printing system cannot use continuous-form printers". More
precisely, **it is not that it cannot — the concept of continuous paper simply does not exist.**

Printing on macOS goes Quartz → PDF → CUPS raster, and what an app produces is
**a set of independent pages with fixed dimensions**. There is no model anywhere for "endless paper
flowing through", so none of this can be expressed:

| What continuous forms need | The macOS counterpart |
|---|---|
| Top of form | None. It is printer state, untouchable from the printing system |
| Perforation feed / reverse feed | None. Neither IPP nor PPD has an attribute for it |
| Printing across page boundaries | Impossible. Pages are independent |
| Imageable area **per paper source** | None. A PPD's `ImageableArea` is **per paper size** |

### How this driver handles it

**One form of continuous paper is defined as a page of that length.** `Cont10x11` is
254 × 279.4 mm (10 × 11 inch). To CUPS it is just a page; when the filter emits a FF at the end of
the page, the printer advances to the next perforation according to its own form length. The form
length is overridden per job with `ESC v`, so it never drifts from the printer's own setting.

**Perforation feed and reverse feed are left to the printer's own functions.** There is no way to
express them on the macOS side, so I decided not to implement them. The machine has them, so nothing
is lost.

The last row of the table above really did bite. Whether to leave 25.4 mm clear around the
perforation is, in the Windows driver, **a choice between two paper sources**: "rear tractor" and
"rear tractor (no top/bottom margin)". A PPD cannot vary the imageable area per source, so I
**defined two paper sizes and told them apart by name** (`連続紙 10x11インチ` and
`連続紙 10x11インチ (ミシン目回避)` — "continuous 10x11 inch" and "… (perforation avoidance)").

### Length itself is not the constraint

People assume "continuous paper means you cannot print long things", but this PPD allows pages of
any length.

```
*MaxMediaHeight: "10000"     (= 10000pt ≒ 3.5m)
```

**A single page can be up to 3.5 m**, so a long table spanning perforations can be printed as long as
you build it as a page of that size. As long as it fits in the frame called a page, length is not a
problem.

### The real problem is the imageable area

macOS trusts `ImageableArea` and rasterizes within it. **Get it wrong and the machine physically
stops, trying to print off the paper.** A modern printer would silently clip; this generation raises
an alarm and halts. I expect this is where most of the time goes when porting.

## Where to go when PPDs stop working

Apple has deprecated classic PPD-based drivers. The destination, when that happens, is an
**IPP Everywhere Printer Application**. Here is the record of actually trying it.

## The conversion code carries over

What an IPP Everywhere / AirPrint printer receives is **PWG Raster or URF (Apple Raster)**.

- **PWG Raster** is based on CUPS Raster v2 — **same magic, same header layout** (`RaS2`). This
  filter parses rasters itself instead of depending on libcups, so it reads them unchanged
- **URF** is a near relative. **Support for reading it has been added** (8-bit gray turned into 1 bit
  with an 8×8 ordered dither)

URF arrives in gray, so the halftone density is ours to decide. Dot-matrix dots spread and touch
their neighbours, so a straight dither **looks almost solid once half the dots are on** (measured:
50–90% collapsed into 49–56%). Adding a correction that works back from the measured values gives
**roughly even steps per 10%** (7, 13, 19, 24, 32, 38, 42, 50, 55%).

Whether to halftone at all (bi-level or grayscale) is chosen with the **media type** in the print
dialog:

| iPhone | Mac | What happens |
|---|---|---|
| Auto (default) | (unset) | **Bi-level**: print the dot if it is darker than half. No correction, no dither |
| Plain paper | Stationery | Bi-level |
| **Photo** | **Photo paper** | **Grayscale**: the correction above plus an ordered dither |

"Stationery", "plain paper" and "photo" are the OS's own words for the IPP `stationery` /
`photographic` keywords. The driver never inspects the content to guess bi-level versus gray.

The standard attribute for this (`print-color-mode`) turned out to be unusable:

- **Neither Mac nor iPhone offers a choice for a monochrome printer; both always send `monochrome`**
- Pretending to support color makes the iPhone show "Print in Color", but **in its default state (on)
  it sends nothing**, so whichever default you choose, either the switch reads backwards or you get
  halftones unless you touch it
- `print-content-optimize` (text / photo) fits the meaning best, but neither Mac nor iPhone shows it

Also note the iPhone sends black text not as true black (0) but as **a flat value of 69**
(about 73% gray), so a grayscale printer halftones the text.

Both formats differ from CUPS raster in that **the raster covers the whole page**. CUPS raster only
covers the imageable area, so the filter shifts it back by the margins; doing that to a full-page
raster shifts it twice (8.4 mm down on A4). The filter compares the raster height with the page
height to decide.

## How you add the printer on a Mac decides the format

| How it is added | Requirement | Format received |
|---|---|---|
| `lpadmin -m everywhere` | PWG Raster alone is enough | PWG Raster |
| **System Settings** (Printers & Scanners) | **Both URF advertising and `-r _print,_universal`** | **URF** |

If you advertise only PWG Raster, no AirPrint option appears in the driver list in System Settings.
**To let an ordinary person add it the ordinary way, URF is mandatory.**

Also, **on a Mac where this driver is installed the PPD wins when the model name matches.** You then
get the familiar UI and the data arrives already converted to 201PL; `mi700ippcmd` passes it straight
to the printer.

## What you lose

Added from System Settings, the print dialog ends up like this:

| | PPD (the current way) | IPP Everywhere |
|---|---|---|
| Media names | `A4`, `連続紙 10x11インチ` | Standard sizes get translated (`A4`, `JIS B4`, `Letter`). **Everything else is just dimensions** (`10 x 11`, `55x91mm`) |
| **Perforation avoidance** | Works | **Gone** (collapses into the same-sized normal variant) |
| Paper source | Sheet feeder / sheet guide / rear tractor / front tractor | **Main tray / manual feed / rear tray / bottom tray** (the macOS words) |
| Eject direction | Front / rear | **Front / Rear** inside "Finishing Options" |
| **Print quality** | 4 modes + unset | **Draft / Normal / Best, three steps** (unidirectional draft is unreachable) |
| Bi-level / grayscale | (decided by the app) | **Chosen by media type** (photo = gray, anything else = bi-level) |
| **Bottom region** | Works | **Gone** (no standard attribute for it) |

Giving the paper sources vendor names (`reartractor` …) does not help: **macOS discards anything that
is not a standard IPP keyword.** They are mapped to `main` / `manual` / `rear` / `bottom` and
translated back by `mi700ippcmd`.

### Trying to get perforation avoidance back

Multifunction printers (Brother and others) give their vendor media types
(`com.brother-hagaki-…`) localized names and show them over AirPrint too. I tried the same:

| Attempt | Result |
|---|---|
| A vendor media type (`com.mi700-perforation`) | **Does not appear on iPhone**; appears as the raw keyword on Mac |
| A strings file (`printer-strings-uri`) | **`ippeveprinter` discards the attribute.** On a multifunction printer the hardware itself serves that file |
| The standard type "continuous" with 25.4 mm top/bottom margins | It appears in the list, but **the iPhone does not use per-type margins** (it lays out with the paper size's margins) |

Standard types do appear with localized names on both iPhone and Mac (continuous, multi-part form,
labels, envelope …). But their only real use is **changing what we do based on the type**, as with
"photo means grayscale". For continuous paper that needs perforation avoidance, print from a Mac with
the driver (PPD) — which works through the same `mi700ipp` server as long as the PPD is selected.

**Both of these are solved by serving the strings file yourself.** `ippeveprinter` cannot, but a
Printer Application written with PAPPL can (see
"[A Printer Application built with PAPPL](airprint.en.md#a-printer-application-built-with-pappl)").

### About the claim that "an app can add vendor settings to AirPrint"

AI assistants and web articles sometimes say the following. **As verified in September 2026 on
macOS 27, none of it applies to this printer.**

| What you often read | Reality |
|---|---|
| A "print service extension" (`UIPrintServiceExtension`) can inject a custom settings pane into the print dialog | It is an **iOS / iPadOS (and Mac Catalyst) extension for *finding* printers**. It has exactly one method, `printerDestinations(for:)`, and no ability to add UI |
| `NSPrintPanelAccessorizing` can add custom fields to the print dialog | It exists, but **only for your own app's** print dialog. It has no effect on Finder, browsers, Pages and so on |
| Custom option values are turned into vendor IPP attributes by CUPS and sent to the printer | **They are not.** Printing to a driverless queue with `-o mi700-bottom=6`, `-o com.mi700-bottom=6` and `-o MI700Bottom=6` showed all three dropped by CUPS before reaching `ippeveprinter` |
| Vendors are moving to system extensions or helper apps that extend the print dialog | No evidence found. Business multifunction printers **still ship PPDs** for macOS 26 and 27 |

"Keep AirPrint and show a vendor settings pane from an app" is the **Windows Print Support App**
model. macOS has no equivalent today (the old Print Dialog Extension was a PPD-bound plugin and does
not work with AirPrint).

There were exactly two ways to carry vendor features over AirPrint:

- **Map them onto standard IPP attributes** (paper source, quality, media type — what this repository
  does)
- **Use the driver (PPD) path** (which also works through the `mi700ipp` server)

**There is no reason to migrate right now.** The PPD path has the better UI and works fine as of
macOS 27. This is a record of what to do when the time comes; the implementation lives on the
`ipp-everywhere` branch.

Attribute-file generation, URF reading and the verification results are developed on the
[`ipp-everywhere` branch](https://github.com/yukino1230/multiimpact-700xx-macos-driver/tree/ipp-everywhere).

## What was verified on the real printer

Data produced by the Windows driver was captured through a `FILE:` port and used as the reference.
**None of this is guesswork; it was measured against real output.**

Beyond that, the 201PL reference and the 700XX2/JX3 user's manual were **checked against every control
code the implementation emits** (2026-09-16). No discrepancies. Even the facts found by measurement —
"the LSB is the top dot" and "`ESC T` is in 1/120 inch" — were documented as such.

- **The resolution is 160 dpi in both axes** (square pixels), derived from the squareness of a
  24 × 24 dot full-width character
- `ESC J nnnn` + data = a 24-dot bit image. Three bytes per column, and **within a byte the LSB is the
  top dot**
- `ESC F nnnn` = absolute horizontal position. One dot = 1/160 inch
- `ESC T nn` + LF = vertical feed. **The unit is 1/120 inch** (not 1/180), 0–99
- `ESC m 1/2/3` = paper source. **The sheet feeder also needs `ESC a`** (eject all, then feed all).
  Without it, printing does not start until you press "Ready" after the paper is loaded. That single
  code is the only difference from the sheet guide
- **The coordinate system matches the Windows driver exactly.** Decoding a capture that printed
  correctly onto a real form and running the image back through this driver puts all 5176 inked dots
  at identical coordinates
- **Trying to print off the paper stops the printer.** The panel flashes alarm / high-speed / paper PE.
  On A4, x=0 is 4.7 mm inside the left paper edge, so the right limit is 205.3 mm = 1293 dots. The fix
  is to get `ImageableArea` right in the PPD
- **Band-boundary misalignment from bidirectional printing is fixed by the printer's own "ruled line
  alignment".** Scanning at 1200 dpi measured 0.13 mm before adjustment and 0.011 mm (standard) /
  0.060 mm (draft) after (the dot pitch is 0.159 mm). Dot-column printing follows the **HD adjustment
  for standard and NHS for draft** (not documented in the manual)
- **The raster only covers the imageable area.** The (0,0) of a CUPS raster is not the page origin but
  the top-left of `ImageableArea`. Unless you read `Margins` and `ImagingBoundingBox` from the header
  and shift back, **the content prints up and to the left** (continuous paper has zero margins so it is
  unaffected; this surfaced when measuring cut sheets). CUPS only carries integer points, so about
  0.2 mm is rounded away
- **`FS 05v` (cut-sheet form length) is in neither manual.** It was derived from two Windows captures.
  The unit is 1/120 inch and **+300 (2.5 inch) is added for cut sheets only** (the continuous capture
  is `540` = exactly 4.5 inch; A4 cut sheet is `1703` = 11.69 inch + 2.5 inch). This is **the length of
  the paper**, not the height of the raster. The same goes for `ESC v` (lines) on continuous paper:
  using the raster height on perforation-avoidance paper (25.4 mm top and bottom) makes it two inches
  short and **drifts two inches per page** (fixed in 3.4)
- **SNMP is Host Resources only.** `hrPrinterDetectedErrorState` gives out-of-paper, cover open, paper
  jam and offline. Printer-MIB (1.3.6.1.2.1.43) is not implemented, so paper and ribbon levels and
  installed options cannot be read

## What remains an assumption

- **A3 / B4 support** is read from a table in the 700XX2 documentation; not tried on a real 700XX
- **The imageable area** is calculated from the **recommended print area** in NEC's "paper design
  guide". The maximum print area is not used, because the guide explicitly warns of "paper jams and
  print head failure"
- **Envelope and business card sizes** are copied from the standard paper table; whether they feed is
  unverified
- **The origin offsets** (cut sheet: 4.7 mm left, 1.0 mm top; tractor: 32 mm left, 0 mm top) are
  measured, but **they change with the tractor's physical position**. Re-measure with `mi700align`
  after changing paper
- Whether it works on other 201PL printers

## Design decisions

- **No software position compensation.** Aligning continuous paper is properly done by physically
  moving the tractor; compensating in software splits the responsibility in two, and a stale setting
  then **drifts silently instead of raising an error**
- **Page sizes are the physical paper size.** Matching the Windows form definitions was preferred,
  because then the coordinates line up exactly
- **Perforation avoidance (25.4 mm top and bottom) is carried as a paper size.** It is really a
  property of the paper source, but a PPD's `ImageableArea` can only be set per paper size
