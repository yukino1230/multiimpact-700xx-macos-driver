# Network setup, sharing and printing from outside

*[← back to the README](../README.en.md)　/　日本語: [network.md](network.md)*

Getting the printer onto the LAN, sharing it through one Mac, and printing from outside the LAN.

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
