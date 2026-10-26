# Printing from an iPhone, an iPad or another Mac

*[← back to the README](../README.en.md)　/　日本語: [airprint.md](airprint.md)*

Run the printer as an AirPrint (IPP Everywhere) service and **iPhones, iPads and other Macs** can
print to it. There are two ways to do that, both included in the v4.0 package.

| | What it is | When |
|---|---|---|
| **The PAPPL version** (`mi700app`) | A Printer Application of our own. Paper sources and media names come out in Japanese and it has a settings page | Everyday use |
| `mi700ipp` | Just the stock `ippeveprinter` made resident | A minimal setup to try it |

The reasoning behind all of this is in [what I found out](findings.en.md).

## A Printer Application built with PAPPL

What `ippeveprinter` lacks — it cannot serve a strings file and cannot hold per-queue settings — is
covered by writing your own Printer Application with **[PAPPL](https://www.msweet.org/pappl/)**.
That is [`driver/mi700pappl.c`](../driver/mi700pappl.c); the 201PL assembly is
shared with the CUPS filter ([`driver/mi700enc.c`](../driver/mi700enc.c)) and the paper sizes come from
the same `forms.conf`.

### Using it

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

### Building it yourself

```bash
# Get PAPPL (not in Homebrew, so build from source)
curl -LO https://github.com/michaelrsweet/pappl/releases/download/v1.4.12/pappl-1.4.12.tar.gz
tar xzf pappl-1.4.12.tar.gz
(cd pappl-1.4.12 && ./configure --disable-shared --disable-libjpeg --disable-libpng && make)
driver/build-pappl.sh pappl-1.4.12

# For a universal build for distribution, do this first. 30-60 minutes, once
driver/build-pappl-deps.sh      # builds OpenSSL and PAPPL for both arm64 and x86_64
driver/build-pappl.sh           # finds build/deps automatically and goes universal
driver/build-pkg.sh 4.0         # bundles it in the package (signing and notarization as usual)
```

**PAPPL requires OpenSSL or GnuTLS for TLS** (the macOS Security.framework is not an option).
Homebrew's OpenSSL is arm64-only, so a naive build produces an Apple-Silicon-only binary.
`build-pappl-deps.sh` builds OpenSSL for both architectures, merges them with `lipo`, and builds
PAPPL universal on top. OpenSSL is linked statically, so the resulting binary depends only on stock
macOS libraries (check with `otool -L`).

### What it gets back

| | `ippeveprinter` version | PAPPL version |
|---|---|---|
| Strings file (`printer-strings-uri`) | Discarded | **Served** |
| Media type | Only the OS words (stationery / photo) | **"Plain (bi-level)" / "Photo (grayscale)"** in Japanese |
| Paper source | Mapped onto standard keywords (main tray, manual feed …) | **"Sheet feeder (auto)"** and so on (on Mac; iPhone below) |
| Media names | Dimensions only for non-standard sizes (`10 x 11`) | **The display names from `forms.conf`** |
| Perforation avoidance | Unreachable — the media name collapses | **Selectable as a queue kind** (via the loaded media's margins) |
| Number of queues | One per process | As many as you like per server |

### How far the strings go on iPhone

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

### What I learned about PAPPL

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

### Verified on the real printer (2026-10-13)

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

### The bottom region (`ESC v`'s second parameter) does not stop printing

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

### Would you use an app that prints straight from iOS?

This route works by making one Mac look like an AirPrint printer, but an iPhone or iPad can just as
well **open port 9100 on the printer and send 201PL directly** — no Mac and no driver involved.

The idea is an app where you **photograph a pre-printed form, place the fields on it, save that as a
template, fill it in and print** (the alignment work `mi700align` does, done on screen). Adding ESC/P
(Epson's VP series) would cover almost every dot-matrix printer still in use in Japan. Carbon-copy
multi-part forms can still only be printed on an impact printer, so the fit is jobs where a slip is
filled in and copied on the spot.

**If you would use it, say so in an
[issue #1](https://github.com/yukino1230/multiimpact-700xx-macos-driver/issues/1) (a 👍 is enough).** Mentioning your
printer (make and model) and the kind of form you print helps. If there is demand, I will build it.

### Not tried yet

- **An Intel Mac.** The binaries are universal (arm64 + x86_64) from v4.0-beta2 on, but everything has
  only been run on Apple Silicon
- The PAPPL version on any 201PL printer other than the 700XX

## Running it

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

- [`driver/mkippattr.py`](../driver/mkippattr.py) — builds the attribute file from `forms.conf` (the IPP
  counterpart of `mkppd.py`)
- [`contrib/mi700ippcmd`](../contrib/mi700ippcmd) — called per job; hands URF / PWG Raster to
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

## Printing from an iPhone

**It prints from an iPhone** (confirmed with A4 from Notes). It arrives as URF, the same as a Mac
added from System Settings, and the paper tray, media type, print quality and output tray can be
chosen.

- **Paper sizes can only be picked from `media-ready` (the loaded media).** With only A4 marked as
  ready in the attribute file, A4 was the only size the iPhone offered. All sizes are listed now
- Black text arrives as gray value 69 (see "bi-level / grayscale" above)
