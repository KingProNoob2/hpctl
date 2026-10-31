# HP WMI / GPU power reverse engineering (2026-10-31)

## The path
  app -> \_SB.WMID (PNP0C14, WMI GUID 86CCFD48-205E-4A77-9C48-2021CBEDE341)
      -> WMAA(sizeclass, buffer) -> HWMC(sizeclass, buffer)
      -> command method (STPS = cmd 0x4C, ...)

buffer layout (0x100 bytes):
   +0x00 SGIN = 0x55434553 "SECU"
   +0x04 COMD = 1 read / 2 write
   +0x08 CMDT = command number (0x4C = STPS, 0x01 = GDST, 0x04 = GDKS, 0x07 = GBIF, ...)
   +0x0C DSZI = data size in bytes (<= 0x80 -> copied into WBUF)
   +0x10 ... data      <-- the ASL says 0x80 because CreateField() takes BITS

HARD-WON DETAILS
  * WMAA takes THREE arguments: (unused, size class, buffer).
  * size class MUST be 3. With 1 the result buffer is 8 bytes and every call
    returns RETC=0x5 ("invalid size") without executing anything.
  * DSZI must equal WCDS[CMDT-1] (Name WCDS, Package(0x53)). 0x4C -> 4.
  * payload is at +0x10 (0x80 BITS), not +0x80. Every profile write silently
    sent 0 until this was found, which read back as "profile 0".

## GPU power
  (containers/PEGP writes below fail on boards without \_SB.PCI0.RP09.PEGP:
   the method aborts with AE_NOT_FOUND right after setting HPCM)
  STPS: WTPS = WBUF[0]; if ECOK: Switch(WTPS)
      0 -> EC0.HPCM = 0x04 ; PEGP.GPTP = 0x5C ; NLIM = 1 ; Notify(PEGP,0xC0)
      1 -> EC0.HPCM = 1    ; (if GPFG==0) GPTP = 0x5C ; NLIM = 1
      2 -> EC0.HPCM = 0x02 ; GPTP = 0x5C
      3 -> EC0.HPCM = 0x08 ; GPTP = 0x5C
  GPTP is the GPU power target: 0x5C (92) = boost, 0x46 (70) = reduced.
  Thermal handlers _Q75 lower GPTP down to 0x46 (sets GPFG=1); _Q76 restores 0x5C.

## EC state
  DGTP  EC 0xB7    the EC's GPU power budget - a NUMBER, not watts
                   (values 0x22..0x40 observed; floats with load and profile)
  HPCM  EC 0x95 low nibble = 1  power mode
  CMAX 0xBB = 4, CLOW 0xBA = 0, DPPC 0xC1 = 0, GFXT = 1 (dGPU present)
  Direct EC writes take effect and are REVERTED by the EC within ~3 s.
  nvidia-smi -pl is unsupported (vBIOS locked). Max power limit = 40 W.
  Reading/ writing these needs ec_sys write_support=1, see hpctl ec.

## Tools on this box
  /tmp/sapmod/hpstps.c   call WMAA with any COMD/CMDT/data
                         insmod hpstps.ko comd=2 cmd=0x4C profile=1
  /tmp/sapmod/hpread.c   read ACPI/EC integers (WTPS, ECOK, HPCM, DGTP, ...)
  /tmp/sapmod/sap.c      SAP block access (EXBU @ 0x54367018); mapping failed,
                         ACPI NVS is not in the direct map and ioremap is refused
  /usr/local/lib/hpctlcmd.sh         wrapper: hpcmd.sh <comd> <cmdt> [profile]
  /tmp/dsdt.dsl          full ASL disassembly (iasl)
  (n/a) EC dump made before the experiments
  modprobe ec_sys write_support=1 -> /sys/kernel/debug/ec/ec0/io

## Open questions
  1. HWMC returns a buffer with SIOU/RETC. Dumping RETC tells whether cmd 0x4C
     actually executed - WTPS stayed 1 and HPCM stayed 1 after our calls, so
     STPS may never be reached.
  2. EC mailbox: SMWR(proto, addr, cmd, data) drives SMPR/SMST/SMAD/SMCM/SMD0
     (EC 0x00-0x04). The GPU TGP command is probably in there; code unknown.
  3. PEGP/GPTP/NLIM are External in the DSDT - confirm the device exists at
     runtime (no SSDT under /sys/firmware/acpi/tables contains "GPTP").

## 2026-10-31 18:40 - IT WORKS (partially)

Correct call format (this was the blocker):
  - sizeclass = 3   (Arg1 of WMAA; output buffer 0x80 bytes). sizeclass=1 gives
    an 8-byte result buffer and every request comes back RETC=0x5.
  - DSZI must equal WCDS[CMDT-1] (Name WCDS, Package(0x53) at dsdt.dsl:59558).
    For CMDT=0x4C that is 4, not 1.
  - data goes at buffer+0x80.

Verified:
  /usr/local/lib/hpctlcmd.sh 2 0x4C 4 d0=0     -> profile 0 applied:
      WMID.WTPS 1 -> 0, EC0.HPCM 1 -> 4, EC0.DGTP 35 W -> 34 W
  (all other profiles then abort with AE_NOT_FOUND until a reboot)

Why the abort: STPS also writes ^^PCI0.RP09.PEGP.GPTP / NLIM, and
\_SB.PCI0.RP09.PEGP does not exist in this machine's namespace
(PEGP/GPTP are External in the DSDT; no loaded SSDT defines them).
So the BIOS's GPU power-target path is dead here and only the EC mode (HPCM)
changes. One profile change per boot; a reboot restores HPCM=1 / 35 W.

Profile -> HPCM (from the ASL Switch, unverified for 2/3/4):
  0 -> 0x04   1 -> 0x01   2 -> 0x02   3 -> 0x08

## Fans
  SFCS  = HP WMI cmd 0x29 (write) -> WSMI(0x02, 0x29, 0x04, 0, 0)  (SMI passthrough)
  GPES/SPES = 0x2A (read/write)   -> WSMI(One/0x02, 0x2A, ...)
  EC fields:  SFAN  EC 0xF4 (8)     SFHK  EC 0xE6 (8)
              RPM1/RPM2 EC 0xB2/0xB3 (8 each, tach)
              FTHM  flag in EC 0xF2 area
              FANP  32-bit field in the SAP block (EXBU+0x090)
  hp-wmi hwmon exposes only pwm1_enable (=2) and name=hp.

## 2026-10-31 18:50 - CORRECT DATA OFFSET + PROFILE SWITCHING VERIFIED

CRITICAL: the data area is at buffer **+0x10** (0x80 is a BIT offset in
`CreateField (Arg1, 0x80, ...)`), not 0x80. With data at 0x80 every profile
write silently sent 0 -> profile 0 -> EC HPCM 0x04. That was the whole bug.

Working call (verified, repeatable, no reboot needed):
  /usr/local/lib/hpctlprofile <0|1|2|3|4>

  profile -> WTPS / EC0.HPCM / meaning
     0         0       0x04     quiet / cool (measured DGTP 34 W at one point)
     1         1       0x01     balanced (default)
     2         2       0x02
     3         3       0x08     PERFORMANCE - fan curve ramps (RPM1 0x00 -> 0x7D
                                when the machine was warm)
     4         4       0x01

  Current DGTP floats (34 / 35 / 38 W seen) - it is the EC's dynamic value and
  does NOT follow the profile. nvidia max power limit is 40 W and
  `nvidia-smi -pl` is refused by the vBIOS.

Raw WMI form:
  insmod hpstps.ko comd=2 cmd=0x4C dszi=4 d0=<profile> sizeclass=3
     SGIN "SECU", COMD 2 (write), CMDT 0x4C, DSZI 4, data at +0x10
     -> WMID.WTPS = profile, EC0.HPCM = mode

Fan: SFCS = cmd 0x29 (WSMI(2,0x29,4,0,0), a pure SMI passthrough, no payload
from the buffer). EC fan registers: SFAN 0xF4, SFHK 0xE6, RPM1/RPM2 0xB2/0xB3.
Fan behaviour follows HPCM, i.e. the profile - so profile 3 IS the fan knob.

## 2026-10-31 19:00 - display / iGPU findings (corrected)

Card mapping (WRONG earlier, fixed):
  card0 = 0000:01:00.0  NVIDIA GTX 1650 Ti   (DP-1, HDMI-A-1, both disconnected)
  card1 = 0000:00:02.0  Intel UHD 630        (card1-eDP-1 = the internal 3840x2160 panel)
  -> the panel is wired to the INTEL GPU, so KWin (and any Wayland compositor)
     must render on the iGPU. There is no MUX and no usable "Dx mode"
     (_Q8D is gated on CondRefOf(\_SB.PCI0.RP09.PEGP), which does not exist).
  -> KWIN_DRM_DEVICES is NOT supported in kwin 6.8.2 (not in the binary), so the
     compositor cannot be moved to the dGPU. PRIME offload still works per-app.

Gesture measurement (4-finger Overview, 4K):
  before the floor: 95% busy, ~975-998 MHz
  after  hpctl igpu 700: 99.8% busy, up to 1161 MHz
  package limits: PL1 53 W (long_term), PL2 107 W (short_term) - not the limit;
  the iGPU ceiling is its fused 1150 MHz + 48 EU, it has no power limit of its own.

Changes made for smoothness:
  kwinrc: blurEnabled=false, contrastEnabled=false   (4K compositing on a UHD 630)
  hpctl gpu 600      dGPU clock floor   (was idling at P8/300 MHz)
  hpctl igpu 700     iGPU clock floor   (was dropping to 350 MHz between frames)
  nvidia-powerd enabled
  /etc/hpctl.profile = 1, /etc/hpctl.gpumin = 600, /etc/hpctl.igpumin = 700
  services enabled: hpctl-profile, hpctl-gpuclock, nvidia-powerd

To revert the effect change: kwriteconfig6 --file kwinrc --group Plugins \
    --key blurEnabled true (then qdbus6 org.kde.KWin /KWin reconfigure)
