# Licensing and third-party notices

## Original Beep code

Original Beep-owned code in this restored archive is released under the
[MIT License](LICENSE). The copyright years identify the historical code;
the archive was restored and released in 2026. Existing author and copyright
attributions are preserved.

The MIT grant permits use, modification, and redistribution, including
commercial use, subject to retaining the copyright and license notice.
The warranty and liability disclaimers are in the license text.

## Third-party code

This is a mixed-license source archive. The root MIT license does not relicense
third-party code, remove its conditions, or override an existing license on a
file or component. Beep's modifications to third-party projects remain subject
to those projects' applicable terms. A file's absence of a license header does
not establish that it is original Beep code.

The following map points to major bundled components and embedded third-party
material. It is a guide, not a replacement for their license texts or a complete
file-by-file attribution inventory.

| Location | Applicable notices |
| --- | --- |
| `device/openwrt` | [OpenWrt license](device/openwrt/LICENSE), together with component and file notices |
| `device/openwrt-packages` | Package-specific source headers and build-recipe license information |
| `device/luci` | [LuCI license](device/luci/LICENSE), [notice](device/luci/NOTICE), and subcomponent notices, including nixio and axTLS |
| `device/libubox`, `device/ubus`, `device/uci`, `device/uhttpd2` | License and copyright notices in the individual source files; these libraries do not all use the same license |
| `device/src/gmrender-resurrect` | [COPYING](device/src/gmrender-resurrect/COPYING) and source headers |
| `device/lttng-modules` | [LICENSE](device/lttng-modules/LICENSE) and source headers |
| `bootloaders/u-boot-1.1.4` | [U-Boot COPYING](bootloaders/u-boot-1.1.4/u-boot/COPYING) and source headers |
| `bootloaders/u-boot_mod` | [U-Boot COPYING](bootloaders/u-boot_mod/u-boot/COPYING) and source headers |
| `third_party/mDNSResponder` | [LICENSE](third_party/mDNSResponder/LICENSE) and component/file notices |
| `device/src/iomcu/lib/stdperiph` | STMicroelectronics headers referencing the MCD-ST Liberty SW License Agreement V2; these vendor files are not covered by Beep's MIT grant |
| `device/src/lib/audio` | Logitech-derived files identify BSD licensing; `fixed_math.c` and `fixed_math.h` identify the Artistic License. Preserve their attributions and terms |
| `device/src/lib/libds` | Peter Bozarov's license in the source headers, including its advertising acknowledgement condition |
| `device/src/beepcomm/mongoose.c` and `.h` | Sergey Lyubka's MIT license and copyright notice in the file headers |
| `device/src/beepjs/external`, other imported JavaScript, and web assets | Original library notices and source attributions, including Node-derived modules, Punycode, jQuery, and other libraries |
| `tools/beep-packages/pybonjour` | [Existing license notice](tools/beep-packages/pybonjour/LICENSE) and source attributions |
| Drivers, patches, and imported code elsewhere in the tree | Their existing source/component notices; placement inside a Beep directory does not change upstream ownership or licensing |

Redistribution of combined or modified programs must satisfy all applicable
component licenses, including GPL/LGPL requirements where they apply. The MIT
license on original Beep code does not remove those obligations.

The Spotify and Pandora SDKs and integrations are excluded from this archive.
