# Beep device source archive

Historical source for the original Beep audio device, prepared from release
`v0.9.12r2` (June 15, 2015). This copy contains the embedded Linux software,
STM8L input-controller firmware source, bootloader source, and selected tools.

This is a source archive for restoration work. It has not been rebuilt or tested
on hardware. The original company's online services are not supplied, and the
Spotify and Pandora integrations have been removed.

## Start here

| Path | Purpose |
| --- | --- |
| `device/src` | Linux device programs, audio playback, discovery, controller interface, Wi-Fi setup, updates, and device-side services |
| `device/src/beephead/README.md` | Historical controller API documentation: JSON-RPC on port 8070 at `/synapse` |
| `device/src/iomcu/controller` | Input-controller application: knob, LEDs, I²C, startup, and standalone behavior |
| `device/src/iomcu/bootloader` | Input-controller bootloader source |
| `device/src/iomcu/common` | Shared Linux/controller protocol definitions |
| `device/src/iomcu/ioutil` | Linux-side I²C/controller utility |
| `device/src/iomcu/iar/iomcu.eww` | IAR STM8 workspace, with bootloader/controller projects and linker configuration |
| `device/ath_i2s` | Audio interface driver source |
| `device/openwrt` | OpenWrt source at the commit pinned by this release |
| `device/openwrt-packages-beep` | Beep firmware packages, configuration, and build integration |
| `bootloaders` | Two separately archived Beep-related U-Boot source trees |
| `tools` | Serial flashing, controller programming-file conversion, update-package tools, debugging, discovery, and network helpers |

The eight original submodule dependencies are included as ordinary source
directories at their exact pinned commits. There is no need to retrieve old
private Git submodules. Additional U-Boot and mDNSResponder snapshots come from
the saved repositories' checked-out commits; they are not claimed to be the
exact versions used for a shipped image. See `SOURCE_PROVENANCE.json`.

## Historical build environment

The original Linux instructions targeted Ubuntu 14.04 and used OpenWrt, SCons,
Python 2, Lua 5.1, a MIPS toolchain, and older audio/networking libraries. The
SCons target definitions include `host`, `beepone`, and `beeptwo` in
`device/src/site_scons/targetdb.py`.

Build entry points are `device/openwrt/Makefile`, the local OpenWrt feed
configuration, and `device/src/SConstruct`. The OpenWrt tree references the
adjacent package and LuCI directories. Downloads, external build dependencies,
and old scripts still require restoration work; this is not a verified modern
build recipe. `device/build_submodules.sh` is a historical helper that installs
libraries into its host system, so use an isolated build environment.

The controller source targets STM8L parts and retains the ST peripheral-library
source and its notices. The IAR projects have Debug/Release configurations,
including standalone controller and B3-main/demo bootloader variants. Match
the configuration and linker file to the actual board before building.
The compiler/toolchain itself is not included.

## Tools and credentials

- `tools/hex2stflash.py` converts controller HEX output to programming data.
- `tools/flasher/flasher.py` and `ymodemsend.sh` implement historical serial
  flashing workflows. The flasher's Wi-Fi settings are explicit placeholders.
- `tools/bpktool.py` and `tools/beep-packages/beep/update` handle update packages,
  image inspection, signing, and verification using caller-supplied material.
- `tools/beepgdb.py`, `beepver.py`, `configdiff.py`, `nmt.py`, and `pings.py`
  provide development and diagnostic helpers.
- SSH helpers use `BEEP_SSH_KEY` (default `~/.ssh/id_rsa`). Password mode uses
  `BEEP_SSH_PASSWORD`. Optional test-AP tooling uses `BEEP_TEST_AP_HOST`.

Original company SSH identities, factory authorizations, password hashes, and
device authentication values are absent. Account password fields in exported
image defaults are locked. Configure your own access during build/provisioning.
Public firmware-verification keys and protocol constants are retained; no
private signing keys are supplied.

## Deliberate omissions and limitations

- No original Git history, mobile apps, cloud backend, analytics, business data,
  hardware CAD, or Beep Networks project.
- No Spotify/Pandora SDKs, headers, libraries, examples, application code, or
  SDK release packages. Their direct build/install steps and default app startup
  registrations were removed. Historical references may remain in protocol
  handling, comments, and old integration tests; those services are unavailable.
- No prebuilt device/controller firmware, binary libraries, opaque archives,
  or factory images. Some old flashing, IDE, and packaging scripts refer to
  outputs that must be rebuilt or supplied locally.
- No original reverse-SSH tunnel service or company factory/database tooling.
  The device-side cloud client remains as source for understanding the protocol;
  old service URLs and service-dependent tests are historical references.
- Bundled axTLS private keys, including the default C-array key, were removed.
  Its optional default-key header now stops compilation with an explanation.
  Generate local material before enabling that backend. Some upstream TLS test
  fixtures and unrelated binary resources are also omitted.
- Third-party source retains its existing copyright/license notices. No blanket
  license has yet been assigned to Beep-owned files in this prepared copy; that
  decision remains with the owner before publication.

Existing test suites are historical source, not evidence that this export
builds or works. The preparation checks cover exclusions, secret scanning,
selected edited-file syntax, provenance, and isolation from the old Git history.
