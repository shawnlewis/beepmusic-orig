#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Kernel defines.
#define __packed __attribute__((__packed__))

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

// Taken from drivers/net/wireless/ath/ath9k/eeprom.h
#define AR_EEPROM_MODAL_SPURS   5

// Prevent compiler errors from unneeded structures.
struct ath_hw;
struct ath9k_channel;

#include "ar9003_eeprom.h"

// File size as read out from /def/mtdX with dd
#define ART_BLOCK_SIZE                              (64*1024)
#define ART_BLOCK_ETH_OFFSET                        (0)
#define ART_BLOCK_ETH0_OFFSET                       (6)
#define ART_BLOCK_ETH1_OFFSET                       (0)
#define ART_BLOCK_ART_OFFSET                        (0x1000)

static const char hex_char[] = "0123456789abcdef";

static const char *mac_str(const uint8_t *buf) {
    static char mac_str[18] = "xx:xx:xx:xx:xx:xx";
    int i;
    for (i = 0; i < 6; i++) {
        mac_str[i * 3] = hex_char[*buf >> 4];
        mac_str[(i * 3) + 1] = hex_char[*buf++ & 0xf];
    }
    return mac_str;
}

static const char *hex_str(const void *b, size_t len) {
    static char str[65];
    const uint8_t *buf = b;
    char *p = str;
    assert((len * 2) < sizeof(str));
    while (len--) {
        *p++ = hex_char[*buf >> 4];
        *p++ = hex_char[*buf++ & 0xf];
    }
    *p = '\0';
    return str;
}

static const char *safe_str(const uint8_t *buf, size_t len) {
    static char str[33];
    char *p = str;
    assert(len < sizeof(str));
    while (len--) {
        if (*buf >= ' ' && *buf <= '~') {
            *p++ = *buf++;
        } else {
            *p++ = '.';
            buf++;
        }
    }
    *p = '\0';
    return str;
}

static void dump_8(size_t offset, size_t size, const char *name,
        const u8 *data, bool sig) {
    int i;
    printf("%04zx:%02zx  %s:%s", offset, size, name,
            (size > 8) ? "\n          " : "");
    for (i = 0; i < size; i++) {
        if (i && (i % 8 == 0)) {
            printf("\n          ");
        }
        if (sig) {
            printf(" %d", (int8_t)data[i]);
        } else {
            printf(" 0x%02x", data[i]);
        }
    }
    printf("\n");
}

static void dump_u8(size_t offset, size_t size, const char *name,
        const u8 *data) {
    dump_8(offset, size, name, data, false);
}

static void dump_s8(size_t offset, size_t size, const char *name,
        const s8 *data) {
    dump_8(offset, size, name, (const u8 *)data, true);
}

static void dump_eth_block(const uint8_t *mac_block) {
    printf("eth block:\n");
    printf("  eth0 mac: %s\n", mac_str(mac_block + ART_BLOCK_ETH0_OFFSET));
    printf("  eth1 mac: %s\n\n", mac_str(mac_block + ART_BLOCK_ETH1_OFFSET));
}

static void dump_ar9300_base_eep_hdr(size_t base,
        struct ar9300_base_eep_hdr *hdr) {
    printf("%04zx:%02zx  RegulatoryDomain[0]: %s\n",
            offsetof(struct ar9300_base_eep_hdr, regDmn) + base,
            sizeof(hdr->regDmn) / 2,
            hex_str(hdr->regDmn,
            sizeof(hdr->regDmn) / 2));
    printf("%04zx:%02zx  RegulatoryDomain[1]: %s\n",
            offsetof(struct ar9300_base_eep_hdr, regDmn) + base + (sizeof(hdr->regDmn) / 2),
            sizeof(hdr->regDmn) / 2,
            hex_str(hdr->regDmn + 1,
            sizeof(hdr->regDmn) / 2));

    printf("%04zx:%02zx  Mask: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, txrxMask) + base,
            sizeof(hdr->txrxMask),
            hdr->txrxMask);
    printf("           Mask.Tx: 0x%02x\n", hdr->txrxMask >> 4);
    printf("           Mask.Rx: 0x%02x\n", hdr->txrxMask & 0xf);

    printf("%04zx:%02zx  OpFlags: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, opCapFlags.opFlags) + base,
            sizeof(hdr->opCapFlags.opFlags),
            hdr->opCapFlags.opFlags);
    printf("           0x01 %s 5GHz\n", hdr->opCapFlags.opFlags & 0x01 ? "enable" : "disable");
    printf("           0x02 %s 2GHz\n", hdr->opCapFlags.opFlags & 0x02 ? "enable" : "disable");
    printf("           0x04 %s 2GHz HT20\n", hdr->opCapFlags.opFlags & 0x04 ? "disable" : "enable");
    printf("           0x08 %s 2GHz HT40\n", hdr->opCapFlags.opFlags & 0x08 ? "disable" : "enable");
    printf("           0x10 %s 5GHz HT20\n", hdr->opCapFlags.opFlags & 0x10 ? "disable" : "enable");
    printf("           0x20 %s 5GHz HT40\n", hdr->opCapFlags.opFlags & 0x20 ? "disable" : "enable");

    printf("%04zx:%02zx  EepMisc: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, opCapFlags.eepMisc) + base,
            sizeof(hdr->opCapFlags.eepMisc),
            hdr->opCapFlags.eepMisc);
    printf("           0x01 %s endian\n", hdr->opCapFlags.eepMisc & 0x01 ? "big" : "little");

    printf("%04zx:%02zx  RfSilent: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, rfSilent) + base,
            sizeof(hdr->rfSilent),
            hdr->rfSilent);
    printf("%04zx:%02zx  RfSilent.HardwareEnable: 0x??\n",
            offsetof(struct ar9300_base_eep_hdr, rfSilent) + base,
            sizeof(hdr->rfSilent));
    printf("%04zx:%02zx  RfSilent.Polarity: 0x??\n",
            offsetof(struct ar9300_base_eep_hdr, rfSilent) + base,
            sizeof(hdr->rfSilent));
    printf("%04zx:%02zx  RfSilent.Gpio: 0x??\n",
            offsetof(struct ar9300_base_eep_hdr, rfSilent) + base,
            sizeof(hdr->rfSilent));

    printf("%04zx:%02zx  BlueToothOptions: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, blueToothOptions) + base,
            sizeof(hdr->blueToothOptions),
            hdr->blueToothOptions);
    printf("%04zx:%02zx  DeviceCapability: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, deviceCap) + base,
            sizeof(hdr->deviceCap),
            hdr->deviceCap);
    printf("%04zx:%02zx  DeviceType: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, deviceType) + base,
            sizeof(hdr->deviceType),
            hdr->deviceType);
    printf("%04zx:%02zx  PowerTableOffset: %+d\n",
            offsetof(struct ar9300_base_eep_hdr, pwrTableOffset) + base,
            sizeof(hdr->pwrTableOffset),
            hdr->pwrTableOffset);

    // Used by ar9003_hw_apply_tuning_caps to set AR_CH0_XTAL_CAP{IN,OUT}DAC
    // with params_for_tuning_caps[0] & 0x7f.
    dump_u8(
            offsetof(struct ar9300_base_eep_hdr, params_for_tuning_caps) + base,
            sizeof(hdr->params_for_tuning_caps),
            "tuning cap params",
            hdr->params_for_tuning_caps);
    dump_u8(
            offsetof(struct ar9300_base_eep_hdr, params_for_tuning_caps) + base,
            sizeof(hdr->params_for_tuning_caps),
            "tuning cap params",
            hdr->params_for_tuning_caps);

    printf("%04zx:%02zx  feature enable: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, featureEnable) + base,
            sizeof(hdr->featureEnable),
            hdr->featureEnable);
    printf("           0x01 %s tx temp comp\n", (hdr->featureEnable & 0x01) ? "enable" : "disable");
    printf("           0x02 %s tx volt comp\n", (hdr->featureEnable & 0x02) ? "enable" : "disable");
    printf("           0x04 %s fast clock\n", (hdr->featureEnable & 0x04) ? "enable" : "disable");
    printf("           0x08 %s doubling\n", (hdr->featureEnable & 0x08) ? "enable" : "disable");
    printf("           0x10 %s internal regulator\n", (hdr->featureEnable & 0x10) ? "enable" : "disable");
    printf("           0x20 %s paprd\n", (hdr->featureEnable & 0x20) ? "enable" : "disable");
    printf("           0x40 %sapply tuning caps\n", (hdr->featureEnable & 0x40) ? "do not " : "");

    printf("%04zx:%02zx  misc configuration: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, miscConfiguration) + base,
            sizeof(hdr->miscConfiguration),
            hdr->miscConfiguration);
    printf("           0x01 %s driver strength\n", (hdr->miscConfiguration & 0x01) ? "enable" : "disable");
    printf("           0x02 %s quick drop\n", (hdr->miscConfiguration & 0x02) ? "enable" : "disable");
    printf("           0x08 %s chain mask reduce\n", (hdr->miscConfiguration & 0x08) ? "enable" : "disable");

    printf("%04zx:%02zx  eeprom write gpio: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, eepromWriteEnableGpio) + base,
            sizeof(hdr->eepromWriteEnableGpio),
            hdr->eepromWriteEnableGpio);
    printf("%04zx:%02zx  wlan disable gpio: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, wlanDisableGpio) + base,
            sizeof(hdr->wlanDisableGpio),
            hdr->wlanDisableGpio);
    printf("%04zx:%02zx  wlan led gpio: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, wlanLedGpio) + base,
            sizeof(hdr->wlanLedGpio),
            hdr->wlanLedGpio);
    printf("%04zx:%02zx  rx band select gpio: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, rxBandSelectGpio) + base,
            sizeof(hdr->rxBandSelectGpio),
            hdr->rxBandSelectGpio);

    printf("%04zx:%02zx  txrx gain: 0x%02x\n",
            offsetof(struct ar9300_base_eep_hdr, txrxgain) + base,
            sizeof(hdr->txrxgain),
            hdr->txrxgain);
    printf("           tx gain: 0x%02x\n", hdr->txrxgain >> 4);
    printf("           rx gain: 0x%02x\n", hdr->txrxgain & 0xf);

    printf("%04zx:%02zx  swreg: 0x%s\n",
            offsetof(struct ar9300_base_eep_hdr, swreg) + base,
            sizeof(hdr->swreg),
            hex_str(&hdr->swreg,
            sizeof(hdr->swreg)));
}

static void dump_ar9300_modal_eep_header(size_t base,
        struct ar9300_modal_eep_header *hdr) {
    int i;

    printf("%04zx:%02zx  ant ctrl common: 0x%s\n",
            offsetof(struct ar9300_modal_eep_header, antCtrlCommon) + base,
            sizeof(hdr->antCtrlCommon),
            hex_str(&hdr->antCtrlCommon,
            sizeof(hdr->antCtrlCommon)));
    printf("%04zx:%02zx  ant ctrl common2: 0x%s\n",
            offsetof(struct ar9300_modal_eep_header, antCtrlCommon2) + base,
            sizeof(hdr->antCtrlCommon2),
            hex_str(&hdr->antCtrlCommon2,
            sizeof(hdr->antCtrlCommon2)));

    for (i = 0; i < AR9300_MAX_CHAINS; i++) {
        printf("%04zx:%02zx  ant ctrl chain[%d]: 0x%s\n",
                offsetof(struct ar9300_modal_eep_header, antCtrlChain[i]) + base,
                sizeof(hdr->antCtrlChain[i]), i,
                hex_str(&hdr->antCtrlChain[i],
                sizeof(hdr->antCtrlChain[i])));
    }


    dump_u8(
            offsetof(struct ar9300_modal_eep_header, xatten1DB) + base,
            sizeof(hdr->xatten1DB),
            "xatten1 DB",
            hdr->xatten1DB);

    dump_u8(
            offsetof(struct ar9300_modal_eep_header, xatten1Margin) + base,
            sizeof(hdr->xatten1Margin),
            "xatten1 margin",
            hdr->xatten1Margin);

    printf("%04zx:%02zx  temp slope: %d\n",
            offsetof(struct ar9300_modal_eep_header, tempSlope) + base,
            sizeof(hdr->tempSlope),
            hdr->tempSlope);
    printf("%04zx:%02zx  volt slope: %d\n",
            offsetof(struct ar9300_modal_eep_header, voltSlope) + base,
            sizeof(hdr->voltSlope),
            hdr->voltSlope);

    dump_u8(
            offsetof(struct ar9300_modal_eep_header, spurChans) + base,
            sizeof(hdr->spurChans),
            "spur chans",
            hdr->spurChans);

    dump_s8(
            offsetof(struct ar9300_modal_eep_header, noiseFloorThreshCh) + base,
            sizeof(hdr->noiseFloorThreshCh),
            "noise floor thresh ch",
            hdr->noiseFloorThreshCh);

    printf("%04zx:%02zx  reserved\n",
            offsetof(struct ar9300_modal_eep_header, reserved) + base,
            sizeof(hdr->reserved));

    printf("%04zx:%02zx  quick drop: %d\n",
            offsetof(struct ar9300_modal_eep_header, quick_drop) + base,
            sizeof(hdr->quick_drop),
            hdr->quick_drop);
    printf("%04zx:%02zx  xpa bias lvl: 0x%02x\n",
            offsetof(struct ar9300_modal_eep_header, xpaBiasLvl) + base,
            sizeof(hdr->xpaBiasLvl),
            hdr->xpaBiasLvl);
    printf("%04zx:%02zx  tx frame to data start: 0x%02x\n",
            offsetof(struct ar9300_modal_eep_header, txFrameToDataStart) + base,
            sizeof(hdr->txFrameToDataStart),
            hdr->txFrameToDataStart);
    printf("%04zx:%02zx  tx frame to pa on: 0x%02x\n",
            offsetof(struct ar9300_modal_eep_header, txFrameToPaOn) + base,
            sizeof(hdr->txFrameToPaOn),
            hdr->txFrameToPaOn);
    printf("%04zx:%02zx  tx clip: 0x%02x\n",
            offsetof(struct ar9300_modal_eep_header, txClip) + base,
            sizeof(hdr->txClip),
            hdr->txClip);
    printf("%04zx:%02zx  antenna gain: %d\n",
            offsetof(struct ar9300_modal_eep_header, antennaGain) + base,
            sizeof(hdr->antennaGain),
            hdr->antennaGain);
    printf("%04zx:%02zx  switch settling: 0x%02x\n",
            offsetof(struct ar9300_modal_eep_header, switchSettling) + base,
            sizeof(hdr->switchSettling),
            hdr->switchSettling);
    printf("%04zx:%02zx  adc desired size: %d\n",
            offsetof(struct ar9300_modal_eep_header, adcDesiredSize) + base,
            sizeof(hdr->adcDesiredSize),
            hdr->adcDesiredSize);
    printf("%04zx:%02zx  tx end to xpa off: 0x%02x\n",
            offsetof(struct ar9300_modal_eep_header, txEndToXpaOff) + base,
            sizeof(hdr->txEndToXpaOff),
            hdr->txEndToXpaOff);
    printf("%04zx:%02zx  tx end ot rx on: 0x%02x\n",
            offsetof(struct ar9300_modal_eep_header, txEndToRxOn) + base,
            sizeof(hdr->txEndToRxOn),
            hdr->txEndToRxOn);
    printf("%04zx:%02zx  tx frame to xpa on: 0x%02x\n",
            offsetof(struct ar9300_modal_eep_header, txFrameToXpaOn) + base,
            sizeof(hdr->txFrameToXpaOn),
            hdr->txFrameToXpaOn);
    printf("%04zx:%02zx  thresh62: 0x%02x\n",
            offsetof(struct ar9300_modal_eep_header, thresh62) + base,
            sizeof(hdr->thresh62),
            hdr->thresh62);
    printf("%04zx:%02zx  papd rate mask ht20: 0x%s\n",
            offsetof(struct ar9300_modal_eep_header, papdRateMaskHt20) + base,
            sizeof(hdr->papdRateMaskHt20),
            hex_str(&hdr->papdRateMaskHt20,
            sizeof(hdr->papdRateMaskHt20)));
    printf("%04zx:%02zx  papd rate mask ht40: 0x%s\n",
            offsetof(struct ar9300_modal_eep_header, papdRateMaskHt40) + base,
            sizeof(hdr->papdRateMaskHt40),
            hex_str(&hdr->papdRateMaskHt40,
            sizeof(hdr->papdRateMaskHt40)));
    printf("%04zx:%02zx  switch com spdt: 0x%s\n",
            offsetof(struct ar9300_modal_eep_header, switchcomspdt) + base,
            sizeof(hdr->switchcomspdt),
            hex_str(&hdr->switchcomspdt,
            sizeof(hdr->switchcomspdt)));
    printf("%04zx:%02zx  xlna bias strength: 0x%02x\n",
            offsetof(struct ar9300_modal_eep_header, xlna_bias_strength) + base,
            sizeof(hdr->xlna_bias_strength),
            hdr->xlna_bias_strength);

    dump_u8(
            offsetof(struct ar9300_modal_eep_header, futureModal) + base,
            sizeof(hdr->futureModal),
            "spur chans",
            hdr->futureModal);
}

static void dump_ar9300_BaseExtension_1(size_t base,
        struct ar9300_BaseExtension_1 *ext) {
    printf("%04zx:%02zx  div control: 0x%02x\n",
            offsetof(struct ar9300_BaseExtension_1, ant_div_control) + base,
            sizeof(ext->ant_div_control),
            ext->ant_div_control);

    dump_u8(
            offsetof(struct ar9300_BaseExtension_1, future) + base,
            sizeof(ext->future),
            "future",
            ext->future);

    dump_u8(
            offsetof(struct ar9300_BaseExtension_1, tempslopextension) + base,
            sizeof(ext->tempslopextension),
            "temp slope extension",
            ext->tempslopextension);

    printf("%04zx:%02zx  quick drop low: %d\n",
            offsetof(struct ar9300_BaseExtension_1, quick_drop_low) + base,
            sizeof(ext->quick_drop_low),
            ext->quick_drop_low);
    printf("%04zx:%02zx  quick drop high: %d\n",
            offsetof(struct ar9300_BaseExtension_1, quick_drop_high) + base,
            sizeof(ext->quick_drop_high),
            ext->quick_drop_high);
}

static void dump_ar9300_cal_data_per_freq_op_loop(size_t base,
        struct ar9300_cal_data_per_freq_op_loop *loop) {
    printf("%04zx:%02zx  ref power: %d\n",
            offsetof(struct ar9300_cal_data_per_freq_op_loop, refPower) + base,
            sizeof(loop->refPower),
            loop->refPower);
    printf("%04zx:%02zx  volt meas: 0x%02x\n",
            offsetof(struct ar9300_cal_data_per_freq_op_loop, voltMeas) + base,
            sizeof(loop->voltMeas),
            loop->voltMeas);
    printf("%04zx:%02zx  temp meas: 0x%02x\n",
            offsetof(struct ar9300_cal_data_per_freq_op_loop, tempMeas) + base,
            sizeof(loop->tempMeas),
            loop->tempMeas);
    printf("%04zx:%02zx  rx noise floor cal: %d\n",
            offsetof(struct ar9300_cal_data_per_freq_op_loop, rxNoisefloorCal) + base,
            sizeof(loop->rxNoisefloorCal),
            loop->rxNoisefloorCal);
    printf("%04zx:%02zx  tx noise floor cal: %d\n",
            offsetof(struct ar9300_cal_data_per_freq_op_loop, rxNoisefloorPower) + base,
            sizeof(loop->rxNoisefloorPower),
            loop->rxNoisefloorPower);
    printf("%04zx:%02zx  rx temp meas: 0x%02x\n",
            offsetof(struct ar9300_cal_data_per_freq_op_loop, rxTempMeas) + base,
            sizeof(loop->rxTempMeas),
            loop->rxTempMeas);
}

static void dump_cal_tgt_pow_legacy(size_t base,
        struct cal_tgt_pow_legacy *pow_leg) {
    dump_u8(
            offsetof(struct cal_tgt_pow_legacy, tPow2x) + base,
            sizeof(pow_leg->tPow2x),
            "t pow 2x",
            pow_leg->tPow2x);
}

static void dump_cal_tgt_pow_ht(size_t base,
        struct cal_tgt_pow_ht *pow_ht) {
    dump_u8(
            offsetof(struct cal_tgt_pow_ht, tPow2x) + base,
            sizeof(pow_ht->tPow2x),
            "t pow 2x",
            pow_ht->tPow2x);
}

static void dump_cal_ctl_data_2g(size_t base,
        struct cal_ctl_data_2g *data) {
    dump_u8(
            offsetof(struct cal_ctl_data_2g, ctlEdges) + base,
            sizeof(data->ctlEdges),
            "ctl edges",
            data->ctlEdges);
}

static void dump_ar9300_BaseExtension_2(size_t base,
        struct ar9300_BaseExtension_2 *ext) {
    printf("%04zx:%02zx  temp slope low: %d\n",
            offsetof(struct ar9300_BaseExtension_2, tempSlopeLow) + base,
            sizeof(ext->tempSlopeLow),
            ext->tempSlopeLow);
    printf("%04zx:%02zx  temp slope high: %d\n",
            offsetof(struct ar9300_BaseExtension_2, tempSlopeHigh) + base,
            sizeof(ext->tempSlopeHigh),
            ext->tempSlopeHigh);
    dump_u8(
            offsetof(struct ar9300_BaseExtension_2, xatten1DBLow) + base,
            sizeof(ext->xatten1DBLow),
            "xatten1 DB low",
            ext->xatten1DBLow);
    dump_u8(
            offsetof(struct ar9300_BaseExtension_2, xatten1MarginLow) + base,
            sizeof(ext->xatten1MarginLow),
            "xatten1 margin low",
            ext->xatten1MarginLow);
    dump_u8(
            offsetof(struct ar9300_BaseExtension_2, xatten1DBHigh) + base,
            sizeof(ext->xatten1DBHigh),
            "xatten1 DB high",
            ext->xatten1DBHigh);
    dump_u8(
            offsetof(struct ar9300_BaseExtension_2, xatten1MarginHigh) + base,
            sizeof(ext->xatten1MarginHigh),
            "xatten 1 margin high",
            ext->xatten1MarginHigh);
}

static void dump_cal_ctl_data_5g(size_t base,
        struct cal_ctl_data_5g *data) {
    dump_u8(
            offsetof(struct cal_ctl_data_5g, ctlEdges) + base,
            sizeof(data->ctlEdges),
            "ctl edges",
            data->ctlEdges);
}

static void dump_art(const uint8_t *art_buf) {
    struct ar9300_eeprom *art = (struct ar9300_eeprom *)(art_buf);
    int i;
    int j;

    printf("art:\n");
    printf("%04zx:%02zx  Version: %u\n",
            offsetof(struct ar9300_eeprom, eepromVersion),
            sizeof(art->eepromVersion),
            art->eepromVersion);
    printf("%04zx:%02zx  Template: %u\n",
            offsetof(struct ar9300_eeprom, templateVersion),
            sizeof(art->templateVersion),
            art->templateVersion);
    printf("%04zx:%02zx  Mac: %s\n",
            offsetof(struct ar9300_eeprom, macAddr),
            sizeof(art->macAddr),
            mac_str(art->macAddr));
    printf("%04zx:%02zx  Customer:\n",
            offsetof(struct ar9300_eeprom, custData),
            sizeof(art->custData));
    printf("           %s [%s]\n",
            hex_str(&art->custData[0], 10),
            safe_str(&art->custData[0], 10));
    printf("           %s [%s]\n",
            hex_str(&art->custData[10], 10),
            safe_str(&art->custData[10], 10));


    //printf("\n->base header:\n");
    dump_ar9300_base_eep_hdr(
            offsetof(struct ar9300_eeprom, baseEepHeader),
            &art->baseEepHeader);

    printf("\n->modal header 2G:\n");
    dump_ar9300_modal_eep_header(
            offsetof(struct ar9300_eeprom, modalHeader2G),
            &art->modalHeader2G);

    printf("\n->base extension 1:\n");
    dump_ar9300_BaseExtension_1(
            offsetof(struct ar9300_eeprom, base_ext1),
            &art->base_ext1);

    printf("\n");
    dump_u8(
            offsetof(struct ar9300_eeprom, calFreqPier2G),
            sizeof(art->calFreqPier2G),
            "cal freq pier 2G",
            art->calFreqPier2G);

    printf("\n");
    for (i = 0; i < AR9300_MAX_CHAINS; i++) {
        for (j = 0; j < AR9300_NUM_2G_CAL_PIERS; j++) {
            printf("->cal pier data 2G[%d][%d]\n", i, j);
            dump_ar9300_cal_data_per_freq_op_loop(
                    offsetof(struct ar9300_eeprom, calPierData2G[i][j]),
                    &art->calPierData2G[i][j]);
        }
    }

    printf("\n");
    dump_u8(
            offsetof(struct ar9300_eeprom, calTarget_freqbin_Cck),
            sizeof(art->calTarget_freqbin_Cck),
            "cal target freqbin cck",
            art->calTarget_freqbin_Cck);
    dump_u8(
            offsetof(struct ar9300_eeprom, calTarget_freqbin_2G),
            sizeof(art->calTarget_freqbin_2G),
            "cal target freqbin 2G",
            art->calTarget_freqbin_2G);
    dump_u8(
            offsetof(struct ar9300_eeprom, calTarget_freqbin_2GHT20),
            sizeof(art->calTarget_freqbin_2GHT20),
            "cal target freqbin 2GHT20",
            art->calTarget_freqbin_2GHT20);
    dump_u8(
            offsetof(struct ar9300_eeprom, calTarget_freqbin_2GHT40),
            sizeof(art->calTarget_freqbin_2GHT40),
            "cal target freqbin 2GHT40",
            art->calTarget_freqbin_2GHT40);

    printf("\n");
    for (i = 0; i < AR9300_NUM_2G_CCK_TARGET_POWERS; i++) {
        printf("->cal target power cck[%d]\n", i);
        dump_cal_tgt_pow_legacy(
                offsetof(struct ar9300_eeprom, calTargetPowerCck[i]),
                &art->calTargetPowerCck[i]);
    }
    printf("\n");
    for (i = 0; i < AR9300_NUM_2G_20_TARGET_POWERS; i++) {
        printf("->cal target power 2G[%d]\n", i);
        dump_cal_tgt_pow_legacy(
                offsetof(struct ar9300_eeprom, calTargetPower2G[i]),
                &art->calTargetPower2G[i]);
    }

    printf("\n");
    for (i = 0; i < AR9300_NUM_2G_20_TARGET_POWERS; i++) {
        printf("->cal target power 2GHT20[%d]\n", i);
        dump_cal_tgt_pow_ht(
                offsetof(struct ar9300_eeprom, calTargetPower2GHT20[i]),
                &art->calTargetPower2GHT20[i]);
    }
    printf("\n");
    for (i = 0; i < AR9300_NUM_2G_40_TARGET_POWERS; i++) {
        printf("->cal target power 2GHT40[%d]\n", i);
        dump_cal_tgt_pow_ht(
                offsetof(struct ar9300_eeprom, calTargetPower2GHT40[i]),
                &art->calTargetPower2GHT40[i]);
    }

    printf("\n");
    dump_u8(
            offsetof(struct ar9300_eeprom, ctlIndex_2G),
            sizeof(art->ctlIndex_2G),
            "ctl index 2G",
            art->ctlIndex_2G);

    for (i = 0; i < AR9300_NUM_CTLS_2G; i++) {
        char *name = NULL;
        assert(asprintf(&name, "ctl freqbin 2G[%d]", i) != -1);
        dump_u8(
                offsetof(struct ar9300_eeprom, ctl_freqbin_2G[i]),
                sizeof(art->ctl_freqbin_2G[0]),
                name,
                art->ctl_freqbin_2G[i]);
        free(name);
    }

    printf("\n");
    for (i = 0; i < AR9300_NUM_CTLS_2G; i++) {
        printf("->cal power data 2G[%d]\n", i);
        dump_cal_ctl_data_2g(
                offsetof(struct ar9300_eeprom, ctlPowerData_2G[i]),
                &art->ctlPowerData_2G[i]);
    }

    printf("\n->modal header 5G:\n");
    dump_ar9300_modal_eep_header(
            offsetof(struct ar9300_eeprom, modalHeader5G),
            &art->modalHeader5G);

    printf("\n->base extension 2:\n");
    dump_ar9300_BaseExtension_2(
            offsetof(struct ar9300_eeprom, base_ext2),
            &art->base_ext2);

    printf("\n");
    dump_u8(
            offsetof(struct ar9300_eeprom, calFreqPier5G),
            sizeof(art->calFreqPier5G),
            "cal freq pier 5G",
            art->calFreqPier5G);

    printf("\n");
    for (i = 0; i < AR9300_MAX_CHAINS; i++) {
        for (j = 0; j < AR9300_NUM_5G_CAL_PIERS; j++) {
            printf("->cal pier data 5G[%d][%d]\n", i, j);
            dump_ar9300_cal_data_per_freq_op_loop(
                    offsetof(struct ar9300_eeprom, calPierData5G[i][j]),
                    &art->calPierData5G[i][j]);
        }
    }

    printf("\n");
    dump_u8(
            offsetof(struct ar9300_eeprom, calTarget_freqbin_5G),
            sizeof(art->calTarget_freqbin_5G),
            "cal target freqbin 5G",
            art->calTarget_freqbin_5G);
    dump_u8(
            offsetof(struct ar9300_eeprom, calTarget_freqbin_5GHT20),
            sizeof(art->calTarget_freqbin_5GHT20),
            "cal target freqbin 5GHT20",
            art->calTarget_freqbin_5GHT20);
    dump_u8(
            offsetof(struct ar9300_eeprom, calTarget_freqbin_5GHT40),
            sizeof(art->calTarget_freqbin_5GHT40),
            "cal target freqbin 5GHT40",
            art->calTarget_freqbin_5GHT40);

    printf("\n");
    for (i = 0; i < AR9300_NUM_5G_20_TARGET_POWERS; i++) {
        printf("->cal target power 5G[%d]\n", i);
        dump_cal_tgt_pow_legacy(
                offsetof(struct ar9300_eeprom, calTargetPower5G[i]),
                &art->calTargetPower5G[i]);
    }

    printf("\n");
    for (i = 0; i < AR9300_NUM_5G_20_TARGET_POWERS; i++) {
        printf("->cal target power 5GHT20[%d]\n", i);
        dump_cal_tgt_pow_ht(
                offsetof(struct ar9300_eeprom, calTargetPower5GHT20[i]),
                &art->calTargetPower5GHT20[i]);
    }
    printf("\n");
    for (i = 0; i < AR9300_NUM_5G_40_TARGET_POWERS; i++) {
        printf("->cal target power 5GHT40[%d]\n", i);
        dump_cal_tgt_pow_ht(
                offsetof(struct ar9300_eeprom, calTargetPower5GHT40[i]),
                &art->calTargetPower5GHT40[i]);
    }

    printf("\n");
    dump_u8(
            offsetof(struct ar9300_eeprom, ctlIndex_5G),
            sizeof(art->ctlIndex_5G),
            "ctl index 5G",
            art->ctlIndex_5G);

    for (i = 0; i < AR9300_NUM_CTLS_5G; i++) {
        char *name = NULL;
        assert(asprintf(&name, "ctl freqbin 5G[%d]", i) != -1);
        dump_u8(
                offsetof(struct ar9300_eeprom, ctl_freqbin_5G[i]),
                sizeof(art->ctl_freqbin_5G[0]),
                name,
                art->ctl_freqbin_5G[i]);
        free(name);
    }

    printf("\n");
    for (i = 0; i < AR9300_NUM_CTLS_5G; i++) {
        printf("->cal power data 5G[%d]\n", i);
        dump_cal_ctl_data_5g(
                offsetof(struct ar9300_eeprom, ctlPowerData_5G[i]),
                &art->ctlPowerData_5G[i]);
    }
}

static void dump_art_block(const uint8_t *art_block) {
    assert(art_block);
    dump_eth_block(art_block + ART_BLOCK_ETH_OFFSET);
    dump_art(art_block + ART_BLOCK_ART_OFFSET);
}

static void usage_and_exit(const char *name, int status) {
    printf("usage: %s [path]\n", name);
    exit(status);
}

int main(int argc, char **argv) {
    size_t rsize;
    FILE *block_file = NULL;
    uint8_t *art_block = NULL;
    int ret = 0;

    if (argc < 2) {
        usage_and_exit(argv[0], 2);
    }

    block_file = fopen(argv[1], "r");
    if (!block_file) {
        printf("fopen failed: %s\n", strerror(errno));
        ret = 1;
        goto done;
    }

    art_block = (uint8_t *)malloc(ART_BLOCK_SIZE);
    if (!art_block) {
        printf("out of memory\n");
        ret = 1;
        goto done;
    }

    rsize = fread(art_block, 1, ART_BLOCK_SIZE, block_file);
    if (rsize != ART_BLOCK_SIZE) {
        ret = 1;
        goto done;
    }

    dump_art_block(art_block);

done:
    if (block_file)
        fclose(block_file);
    if (art_block)
        free(art_block);

    return ret;
}
