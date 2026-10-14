# Using the MultiImpact 700XX from macOS

*日本語版は [README.md](README.md) にあります。*

A CUPS driver for the NEC **MultiImpact 700XX** dot-matrix printer. NEC ships no macOS driver for
this machine, so this is a CUPS filter that builds 201PL (PC-PR201 family) control codes directly.
You print from the ordinary print dialog of any app.

**The current release is [v3.4](https://github.com/yukino1230/multiimpact-700xx-macos-driver/releases/tag/v3.4)**
(signed and notarized packages are on the [Releases](https://github.com/yukino1230/multiimpact-700xx-macos-driver/releases) page).
There is also **[v4.0-beta2](https://github.com/yukino1230/multiimpact-700xx-macos-driver/releases/tag/v4.0-beta2),
a beta that prints from iPhone / iPad as well.** It runs the printer as an AirPrint service without a PPD and
bundles a Printer Application written with [PAPPL](https://www.msweet.org/pappl/) (paper sources and media names
show up in Japanese). Development happens on the
[`pappl` branch](https://github.com/yukino1230/multiimpact-700xx-macos-driver/tree/pappl); see
"[A Printer Application built with PAPPL](#a-printer-application-built-with-pappl-pappl-branch)".

**Dot-matrix printers** are still used for multi-part forms and continuous paper, but the manufacturers
do not ship macOS drivers. This is for people who gave up with "it can't print from a Mac" or
"it only works on Windows". It targets printers that speak **PC-PR201 / PR201 / 201PL**.

## What it does

- **Prints from the normal print dialog.** You can pick the paper source, paper size and print quality
- **No external dependencies.** Rasterizing PDFs is left to the macOS `cgpdftoraster`; only the
  CUPS raster → 201PL conversion is written in C. No Ghostscript, no libcups
- **Paper sizes live in one text file.** Add a line to `forms.conf` and you have a new size
- **Printer status reaches the print queue.** Out of paper or cover open is shown with a reason
- **It prints from an iPhone too (beta v4.0-beta2).** The printer is published as an AirPrint service;
  see "[Where to go when PPDs stop working](#where-to-go-when-ppds-stop-working)"

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

## Putting the printer on the network

The machine has a built-in LAN board with these ports open:

| Port | Use |
|---|---|
| 9100 | RAW. **This is what the driver uses** |
| 515 | LPD |
| 80 | Web UI (the pages are encoded in EUC-JP) |

If the IP address is not set yet, start with the LAN board's own configuration. On my unit the
**initial IP was `11.22.33.44`** (pressing the switch on the NIC prints its settings). I moved a
configuration PC into the same subnet (`11.22.33.55`), opened the board in a browser and changed the
printer name, IP and DHCP from the administrator page. The full procedure is written up here
(Japanese):

- [NEC MultiImpact 700XX: setting up an old impact printer](https://caciocavallo.jp/?p=1116)

Once configured, `http://<IP>/` shows its status. On my unit `/state.html` reports things like out of
paper, and `/opt.html` confirmed the printer language is `201PL`. The admin frame (`/frame.html`)
needs a password.

For a connectivity check this is enough:

```bash
nc -z -v <IP> 9100
snmpget -v1 -c public <IP> 1.3.6.1.2.1.25.3.2.1.3.1   # returns the model name
```

### Use a fixed IP

**This LAN board does not announce itself.** It predates Bonjour/mDNS, so there is no `.local` name
to resolve and browsing for `_pdl-datastream._tcp` finds nothing (verified). The SNMP `sysName` can
only be read over SNMP; it is not name resolution.

A queue points at `socket://<IP>:9100`, so **the queue breaks if the IP changes.** Give the LAN board
a static address, or reserve one on your router if you use DHCP.

Naming it on the Mac side is possible, because it is the Mac that resolves the name, not the printer.
Put it in `/etc/hosts` and you can register the queue by host name:

```
192.168.1.160   mi700
```

```bash
sudo mi700setup mi700 MultiImpact700XX
```

But that only buys a readable name; if the IP changes you still have to edit it. To make it work for
the whole LAN, a static entry in the router's DNS is the proper fix.

Note that **what the print dialog shows is the queue name**, so if you only care about appearances,
passing a queue name as the second argument of `mi700setup` is easier.

### Tidying up the "Add Printer" display (optional)

The macOS "Add Printer" dialog **reads the SNMP `sysLocation` into the Location field**. My unit
shipped with 48 spaces there, so the field was full of blanks. You can rewrite it:

```bash
snmpset -v1 -c public <IP> 1.3.6.1.2.1.1.6.0 s "Office"
```

`sysName` is not used by macOS. If you add the printer by typing an IP by hand, the printer name is
simply that address.

## Installing

A pre-built package is included in the repository, so you can install it as is.

```bash
git clone https://github.com/yukino1230/multiimpact-700xx-macos-driver.git
cd multiimpact-700xx-macos-driver
sudo installer -pkg dist/mi700-3.4.pkg -target /
sudo mi700setup <printer IP>
```

To build it yourself (also what you do after editing `forms.conf`):

```bash
./driver/build-pkg.sh 3.4
sudo installer -pkg build/mi700-3.4.pkg -target /
```

### Signing and notarization (for redistribution)

`build-pkg.sh` **signs automatically if a Developer ID certificate is present**. Without one it just
produces an unsigned package; the build does not fail. For your own use no signature is needed.

To notarize as well, store the `notarytool` credentials in the keychain once:

```bash
xcrun notarytool store-credentials mi700 --apple-id <Apple ID> --team-id <Team ID>
MI700_NOTARY=mi700 ./driver/build-pkg.sh 3.4
```

That signs the filter with the Hardened Runtime, signs the pkg, notarizes it and runs
`stapler staple`. To pick certificates explicitly, use `MI700_SIGN_APP` / `MI700_SIGN_PKG`.

Note that **a package you built from a `git clone` is not blocked by Gatekeeper even unsigned**,
because the quarantine attribute is only attached to files downloaded with a browser.

`mi700setup` creates a `socket://<IP>:9100` queue.

### Setting it up without the terminal

The queue can also be registered from System Settings. The pkg installs by double-clicking, so
**this route never requires opening a terminal.**

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
([tidying it up](#tidying-up-the-add-printer-display-optional)).

You can create as many queues as you like against the same IP, so you can also
**split them by purpose**.

The terminal is only needed for `mi700default` (queue defaults) and `mi700status` (status). Neither
is needed for printing itself.

The package is **signed with a Developer ID and notarized by Apple**. Downloading and
double-clicking is enough. To check:

```bash
spctl -a -vvv -t install mi700-3.4.pkg
# "accepted / source=Notarized Developer ID" means it is fine
```

To remove a queue: `sudo mi700setup --remove <queue name>`.

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

## Sharing and remote printing

The printer is already on the network, so **installing this on each Mac and using it directly is the
simplest thing**. What follows is for routing through one machine, or using it from outside the LAN.

### Sharing through one Mac

You can **install the driver on a single Mac and have the others go through it**. The conversion
happens on the server, so clients only send PDFs.

On the Mac that acts as the server:

```bash
sudo lpadmin -p <queue> -o printer-is-shared=true
sudo cupsctl --share-printers
```

Then turn on System Settings → General → Sharing → Printer Sharing. Other Macs see it over Bonjour
and can just pick it from the Add Printer list.

Queues created by `mi700setup` are `printer-is-shared=false`, so enable sharing explicitly with the
command above.

Setting a description with `-D` makes that the Bonjour name clients see:

```bash
sudo lpadmin -p <queue> -D "MultiImpact 700XX (server name)"
```

#### Install the driver on clients too

A shared printer **can be added without the driver on the client**, but then macOS treats it as
driverless (IPP Everywhere) and **the vendor options are lost.** Both were verified on real hardware:

| | Driver **installed** on the client | **Not installed** |
|---|---|---|
| Paper sizes | All of them, with their Japanese names | Dimensions survive, but names become IPP keywords (`iso_a4_210x297mm` …) |
| **Perforation avoidance** | Works | **Gone.** IPP identifies media by dimensions, so it collapses into the same-sized normal variant |
| Paper source / eject | Works | Works (they are mapped onto the IPP standard `media-source` / `output-bin`) |
| **Print quality, bottom region** | Works | **Gone** |

With the driver installed, macOS builds a client-side PPD that **reuses the choices from this PPD**
and leaves the conversion to the server. The vendor options travel to the server as IPP attributes
and reach the filter (verified by following the job attributes with `ipptool`).

If you choose to run without drivers on the clients, compensate by **setting queue defaults on the
server** (`mi700default`) or by **sharing one queue per purpose**.

### Printing from outside the LAN (Tailscale etc.)

This printer's LAN board is from 1998 and, naturally, does not speak VPN. To use it from outside,
**have a Mac on the LAN bridge for it**. Both of the following were verified with Tailscale (the
MacBook tethered off the LAN, the Mac mini on it).

There are two ways and **both work**:

| | **A. Direct, via a subnet router** | **B. Via CUPS sharing** |
|---|---|---|
| Requires | A node on the LAN advertising `192.168.1.0/24` | Only CUPS sharing on the server |
| Queue device | `socket://192.168.1.160:9100` | `ipp://<server>.ts.net:631/printers/<queue>` |
| Who converts | The client | The server |
| `mi700status` | **Works through the tunnel** | Not reachable from the client |
| What the tailnet sees | **The whole LAN** | **Only that print queue** |

**B is better for security.** A subnet router exposes the whole LAN to the tailnet; sharing exposes
one print queue. **You do not need to enable a subnet router just for a printer.** Conversely, if you
already run one, A is easy and gives you status as well.

#### The trap is `dnssd://`

A queue added over Bonjour has a device URI of `dnssd://…_ipps._tcp.local.`.
**mDNS does not cross subnets, so outside the LAN the name does not resolve and printing fails.**
The route itself is alive and only name resolution breaks, which is a confusing way to fail.

Repointing the device at the VPN FQDN fixes it. **You do not need to recreate the queue:**

```bash
sudo lpadmin -p <queue> -v ipp://<server>.ts.net:631/printers/<queue on the server>
```

The PPD stays, so all options keep working. Back on the LAN, Tailscale picks the direct route and
speed does not suffer.

`ipp://` looks like plaintext, but **WireGuard encrypts it**, so nothing travels in the clear.

#### Requirements on the server side

- **Do not let it sleep.** Check `sleep 0` in `pmset -g`. A sleeping server cannot print
- **CUPS must accept remote connections.** Check that `cupsctl` reports `_remote_any=1`. Note this
  also means **anyone who can reach port 631 can print**. For a machine sitting at home the real risk
  is small, but restrict it with a firewall if you connect to untrusted networks

## Where to go when PPDs stop working

Apple has deprecated classic PPD-based drivers. The destination, when that happens, is an
**IPP Everywhere Printer Application**. Here is the record of actually trying it.

### The conversion code carries over

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

### Running it

**`mi700ipp`** makes it resident. It registers the stock `/usr/bin/ippeveprinter` with launchd and
publishes "MultiImpact 700XX" over Bonjour.

```bash
sudo mi700ipp add 192.168.1.160            # make it resident (runs without anyone logged in)
mi700ipp list                               # list
sudo mi700ipp remove                        # remove
```

| | With sudo | Without sudo |
|---|---|---|
| Registered as | LaunchDaemon | LaunchAgent |
| Runs | **Always** (for a print server) | While that user is logged in |
| Log | `/Library/Logs/mi700/<port>.log` | `~/Library/Logs/mi700/<port>.log` |

Name and port can be changed with `mi700ipp add <IP> "name" --port 8632`.

Once registered, **Macs (added from System Settings) and iPhones** on the same network can print.
**A Mac with this driver installed picks the PPD automatically** and keeps the familiar UI, including
perforation avoidance, printing to the same server (the already-converted data is passed straight
through).

It is three pieces:

- [`driver/mkippattr.py`](driver/mkippattr.py) — builds the attribute file from `forms.conf` (the IPP
  counterpart of `mkppd.py`)
- [`contrib/mi700ippcmd`](contrib/mi700ippcmd) — called per job; hands URF / PWG Raster to
  `rastertomi700`. Already-converted 201PL (starting with `CAN ESC M`) passes through; anything else
  is refused
- The `ippeveprinter` invocation:

```bash
ippeveprinter -D socket://<IP>:9100 -c /usr/local/libexec/mi700/mi700ippcmd \
              -a mi700.conf -r _print,_universal "MultiImpact 700XX"
```

**Some `ippeveprinter` options cannot be combined.** Each merely prints its usage and exits, with no
explanation:

| Combination | Do this instead |
|---|---|
| `-P` (PPD) and `-c` (command) | Put the attributes in the `-a` file |
| `-a` and `-f` (accepted formats) | Put `document-format-supported` in the attribute file |
| `-a` and `-M` / `-m` (make and model) | Put `printer-make-and-model` in the attribute file |

Other things learned by running it:

- **Without `media-size-supported`, jobs that specify media are rejected**
  (`Unsupported media-col collection value`, because they are matched against the defaults
  letter / legal / a4)
- **The paper source arrives inside `IPP_MEDIA_COL`, not `IPP_MEDIA_SOURCE`**
  (`{media-source=rear ...}`)
- Even with `-a`, `ippeveprinter` **adds** its own `document-format-supported`. The attribute appears
  twice, and the first one (from the attribute file) wins
- **`STATE:` / `INFO:` / `ERROR:` / `DEBUG:` on stderr follow the same convention as a CUPS filter**,
  so out-of-paper notifications work as they are. But **unlike CUPS, the state is not cleared when a
  job ends**. Holding on to "media empty" from the start makes the iPhone refuse to send the next job
  ("no paper loaded"), so the filter re-checks the status at the end of the job
- A Mac with the driver (PPD) sends `Create-Job` + `Send-Document`. Sending
  `application/octet-stream` with `Print-Job` makes `ippeveprinter` try to sniff the format and
  refuse it at the door, but real clients do not use that path

### How you add the printer on a Mac decides the format

| How it is added | Requirement | Format received |
|---|---|---|
| `lpadmin -m everywhere` | PWG Raster alone is enough | PWG Raster |
| **System Settings** (Printers & Scanners) | **Both URF advertising and `-r _print,_universal`** | **URF** |

If you advertise only PWG Raster, no AirPrint option appears in the driver list in System Settings.
**To let an ordinary person add it the ordinary way, URF is mandatory.**

Also, **on a Mac where this driver is installed the PPD wins when the model name matches.** You then
get the familiar UI and the data arrives already converted to 201PL; `mi700ippcmd` passes it straight
to the printer.

### What you lose

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

#### Trying to get perforation avoidance back

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
"[A Printer Application built with PAPPL](#a-printer-application-built-with-pappl-pappl-branch)").

#### About the claim that "an app can add vendor settings to AirPrint"

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

### Printing from an iPhone

**It prints from an iPhone** (confirmed with A4 from Notes). It arrives as URF, the same as a Mac
added from System Settings, and the paper tray, media type, print quality and output tray can be
chosen.

- **Paper sizes can only be picked from `media-ready` (the loaded media).** With only A4 marked as
  ready in the attribute file, A4 was the only size the iPhone offered. All sizes are listed now
- Black text arrives as gray value 69 (see "bi-level / grayscale" above)

### A Printer Application built with PAPPL (`pappl` branch)

What `ippeveprinter` lacks — it cannot serve a strings file and cannot hold per-queue settings — is
covered by writing your own Printer Application with **[PAPPL](https://www.msweet.org/pappl/)**.
That is [`driver/mi700pappl.c`](driver/mi700pappl.c) (on the `pappl` branch); the 201PL assembly is
shared with the CUPS filter ([`driver/mi700enc.c`](driver/mi700enc.c)) and the paper sizes come from
the same `forms.conf`.

#### Using it

Installing the package gives you `mi700app` and the binary
(`/usr/local/libexec/mi700/mi700pappl`). After that you just create one queue per paper source.

```bash
sudo mi700app add 192.168.1.160 "MultiImpact 700XX 単票" --source feeder
sudo mi700app add 192.168.1.160 "MultiImpact 700XX 連続紙" --source rear
sudo mi700app add 192.168.1.160 "MultiImpact 700XX 連続紙 ミシン目回避" --source front --perforation
mi700app list
sudo mi700app stop          # stop it (settings are kept; --purge removes them too)
```

- With `sudo` it is a LaunchDaemon, without it a LaunchAgent (same as `mi700ipp`). The default port
  is 8632
- The settings page is at `http://localhost:8632/`, where you can change per-queue defaults and the
  loaded media. Anyone on the LAN can open it, so if that bothers you, start it with
  `-o auth-service=other -o admin-group=admin` and it will ask for a password

#### Building it yourself

```bash
# Get PAPPL (not in Homebrew, so build from source)
curl -LO https://github.com/michaelrsweet/pappl/releases/download/v1.4.12/pappl-1.4.12.tar.gz
tar xzf pappl-1.4.12.tar.gz
(cd pappl-1.4.12 && ./configure --disable-shared --disable-libjpeg --disable-libpng && make)
driver/build-pappl.sh pappl-1.4.12

# For a universal build for distribution, do this first. 30-60 minutes, once
driver/build-pappl-deps.sh      # builds OpenSSL and PAPPL for both arm64 and x86_64
driver/build-pappl.sh           # finds build/deps automatically and goes universal
driver/build-pkg.sh 4.0-beta2   # bundles it in the package (signing and notarization as usual)
```

**PAPPL requires OpenSSL or GnuTLS for TLS** (the macOS Security.framework is not an option).
Homebrew's OpenSSL is arm64-only, so a naive build produces an Apple-Silicon-only binary.
`build-pappl-deps.sh` builds OpenSSL for both architectures, merges them with `lipo`, and builds
PAPPL universal on top. OpenSSL is linked statically, so the resulting binary depends only on stock
macOS libraries (check with `otool -L`).

#### What it gets back

| | `ippeveprinter` version | PAPPL version |
|---|---|---|
| Strings file (`printer-strings-uri`) | Discarded | **Served** |
| Media type | Only the OS words (stationery / photo) | **"Plain (bi-level)" / "Photo (grayscale)"** in Japanese |
| Paper source | Mapped onto standard keywords (main tray, manual feed …) | **"Sheet feeder (auto)"** and so on (on Mac; iPhone below) |
| Media names | Dimensions only for non-standard sizes (`10 x 11`) | **The display names from `forms.conf`** |
| Perforation avoidance | Unreachable — the media name collapses | **Selectable as a queue kind** (via the loaded media's margins) |
| Number of queues | One per process | As many as you like per server |

#### How far the strings go on iPhone

In the iPhone's "Printer Options", the only thing that honoured the strings file was the
**media type**.

| What the iPhone shows | How |
|---|---|
| Media type | **Exactly as in the strings file** (vendor types included) |
| Paper source | **The raw keyword, e.g. `Com.mi700-feeder`** |
| Paper size | Only the loaded media (the OS decides the wording) |
| **Printer name** | **Verbatim** (Japanese included) |

Dropping the `com.` prefix from the source keyword makes macOS discard it entirely, so the shape
cannot be changed. The clearest arrangement for the iPhone turned out to be
**one queue per paper source, named in Japanese**. Picking the printer decides the source and the
media, so no option needs touching. There are three queue kinds (chosen with `--source` and
`--perforation` on `mi700app add`):

| Queue kind | Default source | Loaded media |
|---|---|---|
| `mi700` (cut sheet) | Sheet feeder | A4 |
| `mi700-cont` (continuous) | Rear tractor | Continuous 10x11 inch |
| `mi700-perf` (continuous, perforation avoidance) | Rear tractor | **Continuous 10x11 inch (perforation avoidance)** (25.4 mm top and bottom) |

**Perforation avoidance works purely through the loaded media's margins.** The client only draws
inside those margins, so nothing lands in the band. The bottom region is still sent to the printer
(the `,06` in `ESC v66,06.`), but **that does not stop printing** — see
"[the bottom region does not stop printing](#the-bottom-region-esc-vs-second-parameter-does-not-stop-printing)".

#### What I learned about PAPPL

- **If you use your own system callback, call `papplSystemAddListeners()` yourself.** Otherwise only
  the UNIX domain socket listens and nothing on the network can see it
- **Pass footer HTML or it crashes.** PAPPL 1.4.12 calls
  `papplClientGetLocString(client, NULL)` when `footer_html` is `NULL` and dies in `strcmp(NULL)`.
  The server dies every time a settings page is opened and `KeepAlive` immediately restarts it, so
  all you see is "the page gets cut off"
- **Advertising `black_1` lets the client do the 1-bit conversion itself**, which takes the bi-level
  versus grayscale decision away from you. Accept only `sgray_8`
- **Per-size margins only ride on the loaded media.** PAPPL holds a single "top/bottom" and
  "left/right" pair for the whole printer, which cannot express this machine's asymmetric margins
  (left 1.6 / right 11.0 / top 8.7 / bottom 7.3 mm). You can add your own `media-col-database`, but it
  **duplicates the attribute** PAPPL generates and clients read PAPPL's (all zeros) first, so it is
  pointless. The iPhone only picks loaded media so it does no harm there, but **choosing a size on a
  Mac that is not loaded is treated as zero margins** (the right edge can fall outside the printable
  area)
- Changing the media on the "Media" settings page **resets those margins to 0** (same reason).
  Re-picking the queue kind is the reliable way to change paper
- A vendor media name **is rewritten to the standard one when its dimensions match a standard PWG
  size** (`custom_cont10x11_254x279.4mm` → `na_10x11_10x11in`). So the same-sized "(perforation
  avoidance)" paper **cannot be told apart by name**; the margin (`media-bottom-margin`) is used
  instead
- A `file://` device is rejected unless it points at **an existing writable directory**
- If you set `format`, set `printfile_cb` too. One without the other gives `EINVAL` and the unrelated
  message "printer names must start with a letter"
- State is kept in a state file (`/private/var/lib/mi700pappl.state` as root). When stale settings
  confuse things, `mi700app stop --purge` clears them

#### Verified on the real printer (2026-10-13)

**It prints from an iPhone.** v4.0-beta2 was installed on a Mac mini and made resident as a
LaunchDaemon; from the iPhone's print sheet, picking "MultiImpact 700XX 単票" printed A4. Choosing the
printer already decided the paper source (sheet feeder) and the loaded media (A4), so no option was
touched.

Then 15x11 inch continuous paper was loaded on the **front tractor** and three pages were printed from
the perforation-avoidance queue.

| Checked | Result |
|---|---|
| Paper source | `ESC m1` (front tractor) ✓ |
| Form length | **`ESC v66.`** (66 lines = 11 inch) ✓ |
| Position of the three pages (in the output data) | **Identical on all three — rows 159–1600** (no per-page drift) ✓ |
| Dimensions inside the band (measured) | **≈228 mm** between the rules (228.6 mm by design) ✓ |
| Printing outside the band (measured) | **None** ✓ |
| Position relative to the paper (measured) | **2–3 mm high overall** (23 mm at the top, 28 mm at the bottom), from where the paper was parked and from the origin having been measured on the rear tractor |

The fastest pre-flight check is a queue whose device is `file://`, which runs the same path without
paper and lets you confirm the paper source, form length and page positions
(`tools/mi700decode` turns the output back into an image).

That is also how it became clear that **per-queue settings (vendor options) never reach the job**.
PAPPL only picks vendor options out of the job attributes, so anything the client does not send has to
be read back from the printer's own defaults.

#### The bottom region (`ESC v`'s second parameter) does not stop printing

**Verified on the real printer on 2026-10-13.** With `ESC v66,06.` in effect (bottom region 6 lines =
25.4 mm), a page with lines deliberately placed inside the bands was printed.

| Lines placed | Result |
|---|---|
| Top band (5, 10, 15, 20 mm from the perforation) | **All printed** (the 5 mm line is clipped at the paper edge — a physical limit) |
| Bottom band (258, 262, 266, 270, 274 mm, i.e. below 254 mm) | **All printed**, down to 5.4 mm from the next perforation |
| Spurious page eject | **None** (two pages, two sheets) |

The bottom region is presumably **a setting for automatically ejecting when line feeds reach the
bottom**. This driver places the raster at **absolute positions (`ESC T` and `ESC F`)**, so that
check is never reached.

**Perforation avoidance can therefore only be achieved through the paper definition's margins**
(that is, the client's layout). Exposing "perforation avoidance" as a media type or a vendor option
does nothing if all it does is send the bottom region. The same goes for `MI700Bottom` in the PPD
version: it declares the region but does not stop printing (when you need perforation avoidance,
pick the "(ミシン目回避)" paper size). Sending it is still correct per the specification, so it is
still sent.

#### Not tried yet

- **An Intel Mac.** The binaries are universal (arm64 + x86_64) from v4.0-beta2 on, but everything has
  only been run on Apple Silicon
- The PAPPL version on any 201PL printer other than the 700XX

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
driver/mi700pappl.c      the PAPPL Printer Application (pappl branch)
driver/build-pappl.sh    builds the PAPPL version
driver/mi700app          registers the PAPPL version with launchd and creates queues
contrib/mi700ippcmd      the conversion command ippeveprinter calls
driver/mi700setup        registers a queue
driver/mi700default      sets queue defaults
driver/mi700status       reads printer status over SNMP
forms.conf               paper definitions
tools/mi700print         prints UTF-8 text with ESC K + JIS
tools/pdf2mi700          converts PDF/PS through Ghostscript (the legacy, non-CUPS route)
tools/mi700decode        turns a .prn back into PBM (verification without paper)
tools/mi700align         builds the print-position test pattern
tools/mi700diff          compares two captures
tools/mi700preset        registers a capture as a preset
```

How to use `tools/` is in [Bundled commands](#bundled-commands).

The PPD is written with `*LanguageEncoding: JIS83-RKSJ`, i.e. **Shift-JIS**. UTF-8 makes the print
dialog show mojibake.

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
