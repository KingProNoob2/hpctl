# hpctl

Small Linux CLI for HP laptops that speak HP's ACPI **WMI BIOS** interface
(`\_SB.WMID`, WMI GUID `86CCFD48-205E-4A77-9C48-2021CBEDE341`).

It talks to the firmware directly through ACPI — no HP software, no kernel
patches, no reboot — and exposes what the vendor utility hides:

* **performance profile** (the same EC mode HP Command Center switches),
* **fan / thermal** state and tick,
* **EC RAM** read/write (GPU power budget, mode bits, fan registers),
* **raw HP WMI** calls for any of the ~27 documented commands,
* optional **GPU clock floors**, because Linux lets both GPUs idle too
  aggressively for a 4K desktop.

Everything is read-modify-write on firmware state; nothing is flashed.

```
$ hpctl profile
profile=01 (WTPS)  HPCM=0x01  DGTP=0x29  CMAX=0x04  fan=0 rpm

$ hpctl profile 3
profile=03 (WTPS)  HPCM=0x08  DGTP=0x29  CMAX=0x04  fan=184 rpm
```

## Install

```sh
sudo ./install.sh
```

Needs `linux-headers` (builds two tiny kernel modules), `nvidia-smi` only for
the optional `hpctl gpu` subcommand.

## Usage

```
hpctl profile [0-4]      get / set the EC power profile and persist it
hpctl fan                fan tach + EC fan registers
hpctl info               everything readable through HP WMI
hpctl ec <off> [val]     read/write an EC RAM byte (0xb7 = GPU budget, 0x95 = mode)
hpctl wmi <comd> <cmdt> <dszi> [d0 d1 d2 d3]
hpctl gpu [min]          dGPU clock floor (default 600 MHz)
hpctl igpu [min]         iGPU clock floor (default 700 MHz)
```

Profiles, as measured on a Spectre x360 15-eb (each sets one bit of the EC's
4-bit mode field, and the EC does the rest):

| profile | `EC0.HPCM` | behaviour |
|---|---|---|
| 0 | 0x04 | quiet / cool |
| 1 | 0x01 | balanced (default) |
| 2 | 0x02 | |
| 3 | 0x08 | performance — fan ramps immediately |
| 4 | 0x01 | |

The profile is re-applied at boot by `hpctl-profile.service` from
`/etc/hpctl.profile`; clock floors by `hpctl-gpuclock.service` from
`/etc/hpctl.gpumin` and `/etc/hpctl.igpumin`.

## How it works

`WMAA` is a thin wrapper over `HWMC`, which dispatches on a 0x100-byte request
buffer:

```
+0x00  SGIN = 0x55434553 "SECU"
+0x04  COMD = 1 (read) / 2 (write)
+0x08  CMDT = command number
+0x0C  DSZI = payload size in bytes
+0x10  payload                       <-- NOTE: 0x80 in the ASL is a BIT offset
```

Three details cost the most time to find and are documented in
[docs/PROTOCOL.md](docs/PROTOCOL.md):

1. `WMAA` takes **three** arguments (`arg0` unused, then size class, then buffer).
2. the size class must be **3** — with 1 the result buffer is 8 bytes and every
   request silently fails with `RETC=0x5`;
3. `DSZI` must match the per-command size in the firmware's `WCDS` table
   (4 for `CMDT=0x4C`), and the payload lives at **+0x10**, not +0x80.

`lib/hpstps.c` is a ~120-line module that builds that buffer and calls
`\_SB.WMID.WMAA` via `acpi_evaluate_object`. `lib/hpread.c` reads ACPI integers
(`WTPS`, `HPCM`, `DGTP`, `CMAX`, …) for verification.

## Caveats

* These are undocumented firmware interfaces. A wrong write to EC RAM can wedge
  the embedded controller — a power cycle fixes it, but don't script blind writes.
* The GPU "wattage" (`EC0.DGTP`) is the EC's own dynamic budget, not a value you
  can set: writes are reverted within a few seconds.
* `nvidia-smi -pl` is vBIOS-locked on these machines; the clock floor is the only
  way to keep the dGPU awake enough for a 4K desktop.
* Tested on: HP Spectre x360 15-eb0xxx (i7-10750H + GTX 1650 Ti Max-Q),
  BIOS F.23, Arch, kernel 7.2.11, Plasma 6.8 Wayland. Other HP models using the
  same WMI GUID should work; `hpctl info` will show what your firmware answers.

## License

MIT
