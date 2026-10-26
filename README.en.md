# Using the MultiImpact 700XX from macOS

*日本語版は [README.md](README.md) にあります。*

A CUPS driver for the NEC **MultiImpact 700XX** dot-matrix printer. NEC ships no macOS driver for
this machine, so this is a CUPS filter that builds 201PL (PC-PR201 family) control codes directly.
You print from the ordinary print dialog of any app.

**The current release is [v4.0](https://github.com/yukino1230/multiimpact-700xx-macos-driver/releases/tag/v4.0)**
(signed and notarized packages are on the [Releases](https://github.com/yukino1230/multiimpact-700xx-macos-driver/releases) page).
**It prints from iPhone / iPad and driverless Macs too**: it can run the printer as an AirPrint service
without a PPD, using a bundled Printer Application written with
[PAPPL](https://www.msweet.org/pappl/) (paper sources and media names show up in Japanese). See
"[A Printer Application built with PAPPL](docs/airprint.en.md#a-printer-application-built-with-pappl)".

**Dot-matrix printers** are still used for multi-part forms and continuous paper, but the manufacturers
do not ship macOS drivers. This is for people who gave up with "it can't print from a Mac" or
"it only works on Windows". It targets printers that speak **PC-PR201 / PR201 / 201PL**.

## What it does

- **Prints from the normal print dialog.** You can pick the paper source, paper size and print quality
- **No external dependencies.** Rasterizing PDFs is left to the macOS `cgpdftoraster`; only the
  CUPS raster → 201PL conversion is written in C. No Ghostscript, no libcups
- **Paper sizes live in one text file.** Add a line to `forms.conf` and you have a new size
- **Printer status reaches the print queue.** Out of paper or cover open is shown with a reason
- **It prints from an iPhone or iPad too.** The printer is published as an AirPrint service;
  see "[Printing from an iPhone, an iPad or another Mac](docs/airprint.en.md)"

## Supported printers

| | |
|---|---|
| Verified on real hardware | **MultiImpact 700XX** (built-in LAN board, RAW 9100) |
| Should work per the documentation | **MultiImpact 700XX2 / 700XX2N / 700JX3 / 700JX3N** (**not tested**) |
| Probably works | Other 201PL-family printers (**not tested**) |

For the 700XX2 and 700JX3 I checked the manuals:

- **A single manual covers both the XX2 and the JX3**, and it states explicitly that you simply read
  "700XX2" as "700JX3". The control-code tables do not distinguish them either
- **Every command this driver uses is marked "available" in the extended control-code table**
  (`ESC J` `ESC F` `ESC T` `ESC M` `ESC H` `ESC m` `ESC a` `ESC b` `ESC >` `ESC ]`
  `ESC d` `ESC v` `ESC L` `ESC /` `ESC e` `ESC f` `ESC c8` `FS 05F` `FS 02ER/EF` `EM`)
- **The resolution is documented as "160 dpi only for the MultiImpact 700 series"**, so the scariest
  porting difference — a different resolution — does not occur inside the 700 family

That said, **I have not tried them on real hardware**. The same table marks `ESC n0` / `ESC n1`
(NHS/SHS pica) as unavailable on the XX2/JX3, so **the command sets do differ between the 700XX and
the XX2/JX3**. This driver does not use those, but "should work" is as far as I can go.

The imageable area differs per model, so `forms.conf` will need adjusting in any case.

## Requirements

| | |
|---|---|
| Verified | **macOS 27.0** and **26.6.2** (both Apple Silicon, printing confirmed on real hardware) |
| Probably works | macOS 11 and later (**not tested**) |

Upgrading from 26 to 27 **left the driver, the queues and the PPD in place, and printing kept working**
(an unsigned filter kept working too). However, **the CUPS web interface is disabled** in macOS 27.
Use `mi700default` for queue defaults ([see below](#setting-it-from-a-browser-macos-26-and-earlier)).

The filter is built as a universal binary for arm64 and x86_64. Building needs the
**Xcode Command Line Tools** (`clang`) and **python3**.

```bash
xcode-select --install
```

> **Note:** Apple has deprecated classic PPD-based printer drivers, so this may stop working in a
> future macOS. This printer cannot speak AirPrint by itself, so if that happens you will need a
> workaround such as running your own CUPS. **As of macOS 27.0 everything works** (verified by
> printing on real hardware).

## Installing

**Download `mi700-4.0.pkg` from
[Releases](https://github.com/yukino1230/multiimpact-700xx-macos-driver/releases/latest) and
double-click it.** It is signed with a Developer ID and notarized by Apple, so there is no warning.

To check the signature yourself:

```bash
spctl -a -vvv -t install mi700-4.0.pkg
# "accepted / source=Notarized Developer ID" means it is fine
```

If you cloned the repository, the bundled package works as is:

```bash
sudo installer -pkg dist/mi700-4.0.pkg -target /
```

To build it yourself — which is also what you do after editing `forms.conf`
([signing and notarization](docs/tools.en.md#signing-and-notarization-for-redistribution)):

```bash
./driver/build-pkg.sh 4.0
sudo installer -pkg build/mi700-4.0.pkg -target /
```

## Adding the printer

**No terminal needed.** Add it from System Settings:

1. System Settings → Printers & Scanners → **Add Printer, Scanner, or Fax…**
2. Choose the **IP** tab at the top
3. **Address**: the printer's IP
4. **Protocol**: **HP Jetdirect – Socket** (not LPD)
5. **Queue**: leave empty
6. **Name**: anything you like. **This is what the print dialog shows**
7. **Use**: choose `NEC MultiImpact 700XX`

Step 7 is usually **selected automatically**, because the PPD is installed into
`/Library/Printers/PPDs/Contents/Resources/` and macOS matches the model name it got over SNMP
(`hrDeviceDescr`) against the PPD's `*ModelName`. If it is not, pick it by hand from "Select
Software…".

The Location field comes from the SNMP `sysLocation`
([tidying it up](docs/network.en.md#tidying-up-the-add-printer-display-optional)).

If you prefer the terminal, one line does it. Removing a queue is
`sudo mi700setup --remove <queue name>`.

```bash
sudo mi700setup <printer IP> [queue name]
```

**If the printer has no IP yet, start with [the network setup](docs/network.en.md).**
A queue breaks when the IP changes, so give the printer a static address.

You can create as many queues as you like against the same IP, so you can
[split them by purpose](#separate-queues-for-separate-jobs).


## Using it

In the print dialog, pick the paper source under "Paper Feed" and the eject direction and print
quality under "MultiImpact 700XX". From the command line:

```bash
lp -d <queue> -o PageSize=Cont10x11 -o InputSlot=reartractor -o OutputMode=std-bi file.pdf
```

| Option | Values |
|---|---|
| `InputSlot` | `feeder` (sheet feeder) / `guide` (sheet guide) / `reartractor` / `fronttractor` |
| `OutputBin` | `front` (front eject) / `rear` (stacker) — cut sheets only |
| `OutputMode` | `std-bi` / `std-uni` / `draft-bi` / `draft-uni` / `none` (follow the printer's own setting) |
| `MI700Bottom` | Bottom-region lines `0` / `3` / `6` (declaration only; it does not stop printing — see below) |

`-uni` (unidirectional) is slower but avoids band-boundary misalignment from bidirectional printing.

### Making your usual settings the default

macOS "last used settings" are not restored with this driver (that is Apple's behaviour and a PPD
cannot control it). Set the queue defaults instead:

```bash
mi700default                                    # show current defaults and the available values
sudo mi700default 給紙=リアトラクタ 用紙=Cont10x45   # source=rear tractor, media=Cont10x45
```

The keywords are Japanese because the PPD's UI strings are (`給紙` = paper source, `用紙` = media,
`排出` = eject, `品質` = quality). Run `mi700default` with no arguments to see the exact spellings.

#### Setting it from a browser (macOS 26 and earlier)

> **This does not work on macOS 27.** The CUPS web interface is disabled; `/printers/` and `/admin`
> only answer "the web interface is disabled". **The `WebInterface` setting itself is gone from
> `cupsctl`**, so there is no way to turn it back on (verified). Use `mi700default` on macOS 27.

Up to macOS 26 the CUPS admin pages could do the same thing:

```bash
sudo cupsctl WebInterface=yes
```

Open <http://localhost:631/printers/>, pick the queue, then **Administration → Set Default Options**.
It asks for a Mac administrator account. Choosing the paper source, size, eject direction and quality
and saving gives the same result as `mi700default`. Close it again with
`sudo cupsctl WebInterface=No`.

### Separate queues for separate jobs

You can create any number of queues with different names against the same IP. **Preparing a queue per
purpose, with the paper source and size fixed, means picking the printer in the dialog decides the
settings.** Given that "last used settings" are not restored, this is the most reliable approach.

```bash
sudo mi700setup <IP> MI700_denpyo
sudo mi700default -p MI700_denpyo 給紙=リアトラクタ 用紙=Cont10x45

sudo mi700setup <IP> MI700_a4
sudo mi700default -p MI700_a4 給紙=シートフィーダ 用紙=A4
```

Creating them from System Settings is fine too; set the defaults afterwards.

> **A Japanese queue name mangles the internal name.** The display name in the print dialog stays
> Japanese, but the CUPS queue name becomes something like
> `NEC_MultiImpact_700XX_c_7c__c__c_5c__...`, which is painful to type for `lp -d` or
> `mi700default -p`. **Keep queue names ASCII.** Check the internal name with `lpstat -p`.
>
> If you only want the display name localized, create it with an ASCII name and set the description:
>
> ```bash
> sudo lpadmin -p MI700_denpyo -D "MultiImpact 700XX 伝票"
> ```

### Checking printer status

```bash
mi700status <IP>
```

## Adding paper sizes

`forms.conf` is tab separated:

```
name	display name	width mm	height mm	left mm	right mm	top mm	bottom mm
```

Columns 5–8 are the **non-printable area in page coordinates**. The page origin is not the top-left
corner of the sheet but "the top-left position the print head can reach", so the right and bottom
edges stick out by that much. Setting these to the whole sheet makes
**the printer stop with an error, trying to print off the paper** (see below).

After editing, regenerate the PPD:

```bash
sudo mi700setup <IP> <queue name>     # regenerates the PPD if forms.conf is newer
```

If the print position looks wrong, you can measure the origin with a single sheet:

```bash
mi700align Cont10x11 > a.ps      # a 10 mm ruler pattern
```

How to read it is in [bundled commands](docs/tools.en.md#mi700align--measure-the-print-position);
how continuous forms are handled at all is in
[what I found out](docs/findings.en.md#can-macos-handle-continuous-forms).

## When something goes wrong

| What you see | Where to look |
|---|---|
| **The printer stops with an alarm** (the panel flashes) | It tried to print off the paper. Either the paper size is wrong or the imageable area in `forms.conf` does not match your machine. Measure one sheet with [`mi700align`](docs/tools.en.md#mi700align--measure-the-print-position) |
| **Everything is printed at the wrong position** (continuous paper) | Alignment is done by physically moving the tractor; there is deliberately no software compensation |
| **It stops for out of paper or cover open** | The reason appears in the print queue. `mi700status <IP>` shows it too |
| **The queue stops, or nothing prints** | Check the printer's power and IP. `nc -z -v <IP> 9100` tests reachability. A queue breaks if the IP changes ([use a fixed IP](docs/network.en.md#use-a-fixed-ip)) |
| **No icon** | Coming from 3.x, the queue still holds the old PPD. Re-add it with `sudo mi700setup <IP> <queue name>` |
| **You want to print from an iPhone** | See [printing from an iPhone, an iPad or another Mac](docs/airprint.en.md) |

When you add an unfamiliar paper size, **print one sheet with `mi700align` first and check the
position.** Printing off the paper stops the printer.

## Going deeper

You do not need any of this to use the driver.

| | |
|---|---|
| **[Printing from an iPhone, an iPad or another Mac](docs/airprint.en.md)** | Running the printer as an AirPrint service (`mi700app` / `mi700ipp`), which also serves Macs with no driver installed |
| **[Network, sharing and remote printing](docs/network.en.md)** | The LAN board, a fixed IP, sharing through one Mac, printing from outside over Tailscale |
| **[Bundled commands and porting](docs/tools.en.md)** | Verification tools that use no paper, the icon, signing and notarization, porting to other printers |
| **[What I found out](docs/findings.en.md)** | macOS and continuous forms, life after PPDs (IPP Everywhere / PAPPL), everything verified on the real printer |

## References

Documents published by NEC (not included in this repository), all in Japanese:

- MultiImpact 700 User's Manual
- 201PL Reference Manual
- Dot Printer Paper Design Guide

Setting up the printer itself (the LAN board's IP, checking it from Windows) is written up here:

- [NEC MultiImpact 700XX: setting up an old impact printer](https://caciocavallo.jp/?p=1116) (Japanese)

- [OpenPrinting: the pr201 driver](https://www.openprinting.org/driver/pr201/) — Ghostscript's PR201
  output device

## Disclaimer

Built by one person with one machine. **No warranty**, and no connection with NEC. Printing off the
paper stops the printer. When adding an unfamiliar paper size, print one sheet with `mi700align` first
and check the position.

## License

MIT License. See [LICENSE](LICENSE).
