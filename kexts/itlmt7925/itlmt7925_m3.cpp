// SPDX-License-Identifier: BSD-3-Clause-Clear
// Copyright (c) 2026 thanhnndev and contributors
// Portions derived from the Linux mt76 driver, Copyright (C) 2020, 2023 MediaTek Inc. (BSD-3-Clause-Clear)
// itlmt7925 - M3: join the configured WPA2-PSK network and expose an Ethernet interface.
// Enabled with boot-arg itlmt_m3=1 (implies itlmt_m2=1; the target AP is picked from the M2 scan).
// Credentials come from the kext Info.plist personality keys itl_ssid / itl_psk (filled in locally by scripts/inject-creds.ps1,
// never committed). Progress is logged with m3log() into "m3-log" (mirrored to NVRAM itln0..itln3 by ItlDiag) and a one-line
// summary in "m3-res" (itln4).
// Port of the connac3 connect path in Linux mt76 (BSD-3-Clause-Clear, Copyright (C) MediaTek Inc.): mt7925/main.c, mcu.c, mac.c.
#include "itlmt7925.hpp"
#include "mt7925_regs.h"
#include "itl_wpa.h"

#include <string.h>
#include <stdarg.h>
#include <libkern/libkern.h>
#include <sys/random.h>
#include <kern/clock.h>
#include <mach/mach_time.h>

#define ITL_TIMEOUT -110
#define ITL_EIO     -5
#define ITL_EINVAL  -22

void itlmt7925::m3log(const char *fmt, ...)
{
    char tmp[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    IOLog("itlmt7925: M3 %s\n", tmp);
    size_t n = strlen(tmp);
    if (m3len + n + 2 < sizeof(m3buf)) {
        memcpy(m3buf + m3len, tmp, n);
        m3len += n;
        m3buf[m3len++] = ';';
        m3buf[m3len] = 0;
    }
    setProperty("m3-log", m3buf);
    nvramLog(tmp);                            // last step survives a hang or reboot (variable itlmt7925-log)
    nvramChunks(m3buf);                       // and the whole log (itlk0..itlk4)
}

void itlmt7925::m3Publish(const char *stage)
{
    snprintf(m3res, sizeof(m3res), "%s tgt=%d ch=%u bssid=%02x%02x%02x ie=%u", stage, tgtFound ? 1 : 0, tgtCh, tgtBssid[3],
             tgtBssid[4], tgtBssid[5], tgtIeLen);
    setProperty("m3-res", m3res);
}

// Called by rxParseFrame for every beacon / probe response. `frame` points at the 802.11 header.
void itlmt7925::m3Capture(const uint8_t *frame, uint32_t len, const char *ssid, uint8_t ch)
{
    if (!m3On || !m3ssid[0] || len < 36 || strcmp(ssid, m3ssid) != 0) return;
    // prefer 5 GHz; otherwise keep the first one seen
    if (tgtFound && !(ch > 14 && tgtCh <= 14)) return;
    tgtFound = true;
    memcpy(tgtBssid, frame + 16, 6);
    tgtCh = ch;
    tgtBcnInt = (uint16_t)(frame[32] | (frame[33] << 8));
    tgtCap = (uint16_t)(frame[34] | (frame[35] << 8));
    tgtIeLen = len - 36 > sizeof(tgtIe) ? (uint32_t)sizeof(tgtIe) : len - 36;
    memcpy(tgtIe, frame + 36, tgtIeLen);
}

// ---------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------
static inline uint32_t le32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline void put32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static inline void put16(uint8_t *p, uint16_t v) { memcpy(p, &v, 2); }

#define M3_BSS_IDX     0
#define M3_BMC_WCID    19                   // MT792x_WTBL_RESERVED - bss idx
#define M3_AP_WCID     1

#define UNI_STA_REC_UPDATE   0x03
#define UNI_ROC              0x27
#define UNI_SET_DOMAIN_INFO  0x15
#define UNI_BSS_INFO_UPDATE  0x02

// MCU message under construction: a fixed header (4 bytes BSS_INFO, 8 bytes STA_REC; tlv_num is at offset 2 in both)
// followed by TLVs.
struct M3Msg {
    uint8_t b[640];
    uint16_t n;
    void init(uint16_t hdr) { bzero(b, sizeof(b)); n = hdr; }
    uint8_t *tlv(uint16_t tag, uint16_t len)
    {
        uint8_t *p = b + n;
        put16(p, tag); put16(p + 2, len);
        n += len;
        uint16_t c = (uint16_t)(b[2] | (b[3] << 8)) + 1;
        b[2] = (uint8_t)c; b[3] = (uint8_t)(c >> 8);
        return p;
    }
};

// Sends a UNI command and logs the firmware's status word (event payload: cid, pad[3], status le32 at event offset 48).
int itlmt7925::m3Cmd(uint8_t cid, const void *payload, size_t plen, const char *name)
{
    static uint8_t ev[512];
    uint32_t evl = 0;
    int rc = uniSend(cid, payload, plen, false, ev, sizeof(ev), &evl, 3000);
    m3log("%s rc=%d st=%d", name, rc, evl >= 52 ? (int)le32(ev + 48) : -1);
    return rc;
}

// Everything that arrives on the MCU event ring: frames go to the beacon parser, events to m3Event.
void itlmt7925::m3Pump(uint32_t ms)
{
    for (uint32_t i = 0; i < ms; i++) {
        rxDataPoll();
        const uint8_t *e; uint32_t l; int bi;
        while (ringPeek(rxEvt, &e, &l, &bi)) {
            if (l >= 16) {
                uint32_t d0 = le32(e), type = d0 >> 27, flag = (d0 >> 16) & 0xf;
                if (type == 2 || type == 8 || (type == 7 && flag == 1)) { rxPkts++; rxParseFrame(e, l); }
                else m3Event(e, l);
            }
            ringRelease(rxEvt, bi);
        }
        if (m3sleepy) IOSleep(1); else IODelay(1000);
    }
}

// Unsolicited firmware events we care about.
void itlmt7925::m3Event(const uint8_t *ev, uint32_t len)
{
    if (m3On && len > 48 && ev[36] == 0x04 && m3fwLogged < 24 && (m3wantFc || m3fwLogged < 6)) {   // firmware log text
        m3fwLogged++;
        char t[96];
        uint32_t k = 0;
        for (uint32_t i = 44; i < len && k < sizeof(t) - 1; i++) t[k++] = (ev[i] >= 0x20 && ev[i] < 0x7f) ? (char)ev[i] : '.';
        t[k] = 0;
        m3log("fw: %s", t);
    }
    if (!m3On || m3linkUp || len < 55) return;       // after link up the data path owns the rings; later events are not ROC grants
    if (ev[36] == UNI_ROC) {                  // ROC grant: TLV at 48 {tag, len, bss_idx, tokenid, status}
        rocTok = ev[53];
        rocStatus = ev[54];
        rocGrant = true;
        m3log("roc event bss=%u tok=%u status=%u", ev[52], ev[53], ev[54]);
    }
}

// Rates / RSN of the target AP from the captured beacon IEs.
void itlmt7925::m3ParseAp()
{
    static const uint8_t r5g[8] = {12, 18, 24, 36, 48, 72, 96, 108};    // 500 kb/s units
    const uint8_t *ie = tgtIe, *end = tgtIe + tgtIeLen;
    apSupp = apBasic = 0;
    apRsnLen = 0;
    while (ie + 2 <= end && ie + 2 + ie[1] <= end) {
        uint8_t id = ie[0], l = ie[1];
        if (id == 1 || id == 50) {
            for (int i = 0; i < l; i++)
                for (int k = 0; k < 8; k++)
                    if ((ie[2 + i] & 0x7f) == r5g[k]) { apSupp |= 1u << k; if (ie[2 + i] & 0x80) apBasic |= 1u << k; }
        } else if (id == 48 && l <= sizeof(apRsn) - 2) {
            apRsnLen = (uint8_t)(l + 2);
            memcpy(apRsn, ie, apRsnLen);
        }
        ie += 2 + l;
    }
    m3log("ap rates supp=%02x basic=%02x rsn=%u", apSupp, apBasic, apRsnLen);
    m3FastPlan();
    if (apRsnLen >= 14) {
        // version(2) group(4) pairwise count(2) ...
        const uint8_t *r = apRsn + 2;
        uint16_t pc = (uint16_t)(r[6] | (r[7] << 8));
        m3log("rsn group=%02x pairwise n=%u first=%02x akm1=%02x", r[5], pc, pc ? r[11] : 0,
              (apRsnLen >= 12 + 4 * pc + 6) ? r[8 + 4 * pc + 2 + 3] : 0);
    }
}

// HT/VHT plan (boot-arg itlmt_fast=1): what the AP offers, the channel width to run at and the rate to report to macOS.
// Stays off (legacy 6 Mb/s path, exactly as before) unless the AP is on 5 GHz and advertises HT.
void itlmt7925::m3FastPlan()
{
    fastActive = false;
    if (!m3fast) return;
    itl_caps_parse(tgtIe, tgtIeLen, &apCaps);
    if (tgtCh <= 14 || !apCaps.ht) {
        m3log("fast: off (ch=%u ht=%d)", tgtCh, apCaps.ht ? 1 : 0);
        return;
    }
    itl_caps_channel(&apCaps, tgtCh, &fastBw, &fastCenter);
    fastNss = itl_caps_nss(&apCaps);
    fastRate = itl_caps_max_rate(&apCaps, fastBw);
    fastActive = true;
    m3log("fast: on ht=%d vht=%d bw=%u center=%u nss=%u rate~%u htcap=%04x vhtcap=%08x", apCaps.ht ? 1 : 0, apCaps.vht ? 1 : 0,
          fastBw, fastCenter, fastNss, fastRate, apCaps.htCap, apCaps.vhtCap);
}

// ---------------------------------------------------------------------------------------------
// hardware / firmware prep (mt7925_mac_init, mt7925_mcu_set_channel_domain)
// ---------------------------------------------------------------------------------------------
#define MT_MDP_DCR0              0x820cc800
#define MT_MDP_DCR0_DAMSDU_EN    (1u << 15)
#define MT_MDP_DCR1              0x820cc804
#define MT_WTBL_UPDATE_REG       0x820d4380
#define MT_WTBL_ITCR             0x820d43b0
#define MT_WTBL_ITDR0            0x820d43b8
#define MT_WTBL_ITDR1            0x820d43bc

int itlmt7925::m3HwPrep()
{
    // MDP: max rx length 1536, hardware A-MSDU de-aggregation
    uint32_t v = rdA(MT_MDP_DCR1);
    wrA(MT_MDP_DCR1, (v & ~0xfff8u) | (1536u & 0xfff8u));
    wrA(MT_MDP_DCR0, rdA(MT_MDP_DCR0) | MT_MDP_DCR0_DAMSDU_EN);
    m3log("mdp dcr0=%08x dcr1=%08x", rdA(MT_MDP_DCR0), rdA(MT_MDP_DCR1));

    if (!m3hw) {
        // steps the Linux driver does once after the firmware runs: firmware log to host, eeprom = efuse, RTS threshold
        m3hw = true;
        static const uint8_t fwlog[12] = {0, 0, 0, 0, 0, 0, 8, 0, 1, 0, 0, 0};            // UNI_WSYS_CONFIG_FW_LOG_CTRL, ctrl 1
        static const uint8_t eeprom[12] = {0, 0, 0, 0, 2, 0, 8, 0, 0, 0, 0, 0};          // UNI_EFUSE_BUFFER_MODE: efuse, whole
        static const uint8_t rts[16] = {0, 0, 0, 0, 8, 0, 12, 0, 0x2b, 0x09, 0, 0, 2, 0, 0, 0};   // band 0, len 0x92b, pkt 2
        m3Cmd(0x0b, fwlog, sizeof(fwlog), "fw_log");
        m3Cmd(0x2d, eeprom, sizeof(eeprom), "eeprom(efuse)");
        m3Cmd(0x08, rts, sizeof(rts), "rts_thresh");
    }

    // fixed-rate table: idx 14 + i, i = mt76_rates[]: CCK 1,2,5.5,11 then OFDM 6..54 Mb/s
    static const uint16_t rates[12] = { (0 << 6) | 0, (0 << 6) | 1, (0 << 6) | 2, (0 << 6) | 3,
                                        (1 << 6) | 11, (1 << 6) | 15, (1 << 6) | 10, (1 << 6) | 14,
                                        (1 << 6) | 9, (1 << 6) | 13, (1 << 6) | 8, (1 << 6) | 12 };
    for (int i = 0; i < 12; i++) {
        wrA(MT_WTBL_ITDR0, rates[i]);
        wrA(MT_WTBL_ITDR1, 1u << 6);                         // MT_WTBL_SPE_IDX_SEL
        wrA(MT_WTBL_ITCR, (1u << 16) | (1u << 31) | (14 + i));
    }
    // clear the admission counters of the two wcids we use
    static const uint16_t wcids[2] = { M3_BMC_WCID, M3_AP_WCID };
    for (int i = 0; i < 2; i++) {
        uint32_t u = rdA(MT_WTBL_UPDATE_REG);
        wrA(MT_WTBL_UPDATE_REG, (u & ~0xfffu) | wcids[i] | (1u << 14));
        pollA(MT_WTBL_UPDATE_REG, 1u << 31, 0, 50);
    }

    // channel domain: world regulatory ("00"), every channel we might join
    static const uint16_t ch2[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    static const uint16_t ch5[] = { 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140,
                                    144, 149, 153, 157, 161, 165 };
    static uint8_t dom[16 + 8 * 40];
    unsigned n2 = sizeof(ch2) / sizeof(ch2[0]), n5 = sizeof(ch5) / sizeof(ch5[0]);
    bzero(dom, sizeof(dom));
    dom[0] = '0'; dom[1] = '0';
    dom[4] = 0; dom[5] = 3; dom[6] = 3;                      // bw_2g 20/40, bw_5g 20..160, bw_6g
    put16(dom + 8, 2);                                       // tag
    put16(dom + 10, (uint16_t)(8 + (n2 + n5) * 8));          // len
    dom[12] = (uint8_t)n2; dom[13] = (uint8_t)n5; dom[14] = 0;
    uint8_t *w = dom + 16;
    for (unsigned i = 0; i < n2; i++, w += 8) put16(w, ch2[i]);
    for (unsigned i = 0; i < n5; i++, w += 8) put16(w, ch5[i]);
    return m3Cmd(UNI_SET_DOMAIN_INFO, dom, (size_t)(w - dom), "set_domain");
}

// ---------------------------------------------------------------------------------------------
// BSS_INFO_UPDATE for the station interface (mt7925_mcu_add_bss_info_sta, non-MLD)
// ---------------------------------------------------------------------------------------------
int itlmt7925::m3Bss(bool enable)
{
    M3Msg m;
    m.init(4);
    m.b[0] = M3_BSS_IDX;

    uint8_t *b = m.tlv(0, 32);                               // UNI_BSS_INFO_BASIC
    b[4] = 1;                                                // active (station)
    b[5] = 0;                                                // omac_idx
    b[6] = 0;                                                // hw_bss_idx
    b[7] = 0xff;                                             // band_idx
    put32(b + 8, 0x10001);                                   // CONNECTION_INFRA_STA
    b[12] = enable ? 0 : 1;                                  // conn_state
    b[13] = 0;                                               // wmm_idx
    memcpy(b + 14, tgtBssid, 6);
    put16(b + 20, M3_BMC_WCID);
    put16(b + 22, tgtBcnInt ? tgtBcnInt : 100);
    b[24] = 1;                                               // dtim_period
    b[25] = 0x01;                                            // phymode: 5 GHz legacy (PHY_MODE_A)
    if (fastActive) b[25] |= 0x10 | (apCaps.vht ? 0x20 : 0); // | PHY_MODE_AN | PHY_MODE_AC
    put16(b + 26, M3_AP_WCID);
    put16(b + 28, 3);                                        // nonht_basic_phy = PHY_TYPE_OFDM_INDEX
    b[30] = 0;                                               // phymode_ext
    b[31] = 0;                                               // link_idx

    uint8_t *s = m.tlv(16, 8);                               // UNI_BSS_INFO_SEC
    if (m3cipher == 4) { s[4] = 7; s[5] = 6; s[6] = 4; }     // WPA2-PSK, CCMP
    else { s[4] = 0; s[5] = 1; s[6] = 0; }                   // open

    uint8_t *r = m.tlv(11, 16);                              // UNI_BSS_INFO_RATE
    put16(r + 6, (1 << 6) | (1 << 8) | (1 << 10));           // basic_rate = OFDM_BASIC_RATE
    r[12] = 0;                                               // short_preamble (5 GHz: no)
    r[13] = 18;                                              // bc_fixed_rate: table idx 14 + 4 (OFDM 6M)
    r[14] = 18;                                              // mc_fixed_rate

    uint8_t *q = m.tlv(15, 8);                               // UNI_BSS_INFO_QBSS
    q[4] = 1;

    uint8_t *ml = m.tlv(26, 20);                             // UNI_BSS_INFO_MLD
    ml[4] = 0xff;                                            // group_mld_id
    ml[5] = M3_BSS_IDX + 32;                                 // own_mld_id
    memcpy(ml + 6, mac, 6);
    ml[12] = 0xff;                                           // remap_idx
    ml[13] = 0xff;                                           // link_id

    uint8_t *f = m.tlv(23, 20);                              // UNI_BSS_INFO_IFS_TIME
    f[4] = 1;                                                // slot_valid
    put16(f + 8, 9);                                         // slot_time

    if (enable) {
        uint8_t *rl = m.tlv(2, 16);                          // UNI_BSS_INFO_RLM
        rl[4] = tgtCh;                                       // control_channel
        rl[5] = tgtCh;                                       // center_chan (20 MHz)
        rl[6] = 0;
        rl[7] = 0;                                           // CMD_CBW_20MHZ
        rl[8] = 1; rl[9] = 1;                                // tx / rx streams
        rl[10] = 0;                                          // ht_op_info
        if (fastActive) {                                    // mt7925_mcu_bss_rlm_tlv: AP's width, both streams, "HT 40 allowed"
            rl[5] = fastCenter;
            rl[7] = fastBw;
            rl[8] = rl[9] = 2;
            rl[10] = 4;
        }
        rl[11] = 0;                                          // sco
        rl[12] = tgtCh > 14 ? 2 : 1;                         // band
    }
    return m3Cmd(UNI_BSS_INFO_UPDATE, m.b, m.n, enable ? "bss_info(enable)" : "bss_info(add)");
}

// ---------------------------------------------------------------------------------------------
// STA_REC_UPDATE for the AP (mt7925_mcu_sta_cmd, non-MLD station, legacy OFDM peer)
// state: 0 NONE, 1 AUTH, 2 ASSOC
// ---------------------------------------------------------------------------------------------
int itlmt7925::m3Sta(uint8_t state, bool newly, uint16_t aid)
{
    M3Msg m;
    m.init(8);
    m.b[0] = M3_BSS_IDX;
    m.b[1] = M3_AP_WCID & 0xff;                              // wlan_idx_lo
    m.b[4] = 1;                                              // is_tlv_append
    m.b[5] = 0;                                              // muar_idx = omac_idx
    m.b[6] = M3_AP_WCID >> 8;

    uint8_t *b = m.tlv(0, 20);                               // STA_REC_BASIC
    put32(b + 4, 0x10002);                                   // CONNECTION_INFRA_AP (the peer is an AP)
    b[8] = 2;                                                // CONN_STATE_PORT_SECURE
    b[9] = 1;                                                // qos (WMM)
    put16(b + 10, aid);
    memcpy(b + 12, tgtBssid, 6);
    put16(b + 18, newly ? 3 : 1);                            // EXTRA_INFO_VER | NEW

    bool ht = fastActive && state == 2;                      // capabilities go in with the association, not the early "none" record
    if (ht) {                                                // mt7925_mcu_sta_ht_tlv / vht_tlv: the peer's (the AP's) capabilities
        uint8_t *h = m.tlv(9, 8);                            // STA_REC_HT
        put16(h + 4, apCaps.htCap);
        if (apCaps.vht) {
            uint8_t *v = m.tlv(10, 16);                      // STA_REC_VHT
            put32(v + 4, apCaps.vhtCap);
            put16(v + 8, apCaps.vhtRxMcs);
            put16(v + 10, apCaps.vhtTxMcs);
        }
    }

    uint8_t *p = m.tlv(0x15, 12);                            // STA_REC_PHY
    put16(p + 4, apBasic);                                   // basic_rate
    p[6] = 1u << 3;                                          // PHY_TYPE_BIT_OFDM
    if (ht) {
        uint8_t af = apCaps.ampduParm & 3, mm = (apCaps.ampduParm >> 2) & 7;
        p[6] |= (1u << 4) | (apCaps.vht ? (1u << 5) : 0);    // PHY_TYPE_BIT_HT | PHY_TYPE_BIT_VHT
        if (apCaps.vht) {
            uint8_t vaf = (uint8_t)((apCaps.vhtCap >> 23) & 7);
            if (vaf > af) af = vaf;
        }
        p[7] = (uint8_t)((af & 3) | (mm << 2));              // ampdu: factor | density << 2
        p[10] = af;                                          // max_ampdu_len (connac3)
    }

    uint8_t *ra = m.tlv(1, 16);                              // STA_REC_RA
    put16(ra + 4, (uint16_t)apSupp << 6);                    // legacy: RA_LEGACY_OFDM GENMASK(13, 6)
    if (ht) memcpy(ra + 6, apCaps.mcsRx, sizeof(apCaps.mcsRx));    // rx_mcs_bitmask: HT MCS the AP receives

    uint8_t *st = m.tlv(7, 16);                              // STA_REC_STATE
    st[4] = state;
    if (ht && apCaps.vht) st[12] = (uint8_t)(fastBw | (fastNss << 4));   // vht_opmode: bandwidth | rx_nss << 4

    if (state != 0) {
        uint8_t *ml = m.tlv(0x20, 28);                       // STA_REC_MLD
        memcpy(ml + 4, tgtBssid, 6);
        put16(ml + 10, M3_AP_WCID);                          // primary_id
        put16(ml + 14, M3_AP_WCID);                          // wlan_id
        ml[16] = 1;                                          // link_num
        put16(ml + 20, M3_AP_WCID);                          // link[0].wlan_id
        ml[22] = M3_BSS_IDX;                                 // link[0].bss_idx
    }

    uint8_t *h = m.tlv(0x2b, 8);                             // STA_REC_HDR_TRANS
    h[4] = 0; h[5] = 1;                                      // from_ds, to_ds
    h[6] = 1;                                                // dis_rx_hdr_tran: we handle 802.11 frames ourselves
    char nm[32];
    snprintf(nm, sizeof(nm), "sta_rec(st=%u)", state);
    return m3Cmd(UNI_STA_REC_UPDATE, m.b, m.n, nm);
}

// ---------------------------------------------------------------------------------------------
// ROC (remain on channel) to be allowed to transmit auth / assoc on the AP's channel
// ---------------------------------------------------------------------------------------------
int itlmt7925::m3Roc(bool acquire)
{
    uint8_t req[4 + 24];
    bzero(req, sizeof(req));
    uint8_t *t = req + 4;
    if (acquire) {
        rocGrant = false;
        rocStatus = 0xff;
        rocTok++;
        put16(t + 0, 0);                                     // UNI_ROC_ACQUIRE
        put16(t + 2, 24);
        t[4] = M3_BSS_IDX;
        t[5] = rocTok;
        t[6] = tgtCh;                                        // control_channel
        t[7] = 0;                                            // sco
        t[8] = tgtCh > 14 ? 2 : 1;                           // band
        t[9] = 0;                                            // bw 20 MHz
        t[10] = tgtCh;                                       // center_chan
        t[12] = 0;                                           // bw_from_ap
        t[13] = tgtCh;                                       // center_chan_from_ap
        t[15] = 0;                                           // reqtype JOIN
        put32(t + 16, 5000);                                 // maxinterval (ms)
        t[20] = 0xff;                                        // dbdcband: auto
        return m3Cmd(UNI_ROC, req, sizeof(req), "roc_acquire");
    }
    put16(t + 0, 1);                                         // UNI_ROC_ABORT
    put16(t + 2, 12);
    t[4] = M3_BSS_IDX;
    t[5] = rocTok;
    t[6] = 0xff;                                             // dbdcband
    return m3Cmd(UNI_ROC, req, 4 + 12, "roc_abort");
}

// ---------------------------------------------------------------------------------------------
// Management frame TX / RX (mt7925e_tx_prepare_skb + mt7925_mac_write_txwi, CT path with a token in the hw txp)
// ---------------------------------------------------------------------------------------------
static inline void membar() { __sync_synchronize(); }
#define M3_RATE_IDX 18                                           // fixed-rate table: OFDM 6 Mb/s (see m3HwPrep)

// TXD (32) + hw txp (32) in the ring buffer, the 802.11 frame 128 bytes further; the ring entry covers TXD + txp only.
struct M3Lock {
    IOLock *l;
    explicit M3Lock(IOLock *x) : l(x) { if (l) IOLockLock(l); }
    ~M3Lock() { if (l) IOLockUnlock(l); }
};

bool itlmt7925::m3Tx(const uint8_t *frame, uint32_t len, uint8_t type, uint8_t subtype, bool protect, bool rateAdapt)
{
    if (!txData.n || len > 1900 || len < 24) return false;
    M3Lock lk(txLock);                                           // management frames from the m3 thread, data from the network stack
    size_t base = (size_t)(m3txSlot++ & 63) * 2048;
    uint8_t *w = txData.buf.va + base;
    uint64_t pa = txData.buf.pa + base;
    bzero(w, 128);
    memcpy(w + 128, frame, len);

    put32(w + 0, (len + 32) | (0u << 23) | (0x10u << 25));       // TX_BYTES | PKT_FMT=CT | Q_IDX=ALTX0
    uint32_t hdrLen = (type == 2 && (frame[0] & 0x80)) ? 26 : 24;               // QoS data frames carry the 2-byte QoS control field
    // wlan idx, HDR_FORMAT=802.11, HDR_INFO; FIXED_RATE unless the firmware's rate control (STA_REC_RA) picks the rate
    put32(w + 4, M3_AP_WCID | (2u << 14) | ((hdrLen / 2) << 16) | (rateAdapt ? 0u : (1u << 31)));
    put32(w + 8, ((uint32_t)type << 4) | subtype);
    put32(w + 12, (15u << 11) | (1u << 28) | (protect ? 2u : 0));   // REM_TX_COUNT=15, BA_DISABLE (fixed rate), PROTECT_FRAME
    put32(w + 20, rateAdapt ? 0u : (3 | (1u << 10)));            // PID = MT_PACKET_ID_FIRST, TX_STATUS_HOST: report completion (none for rate-adapted data)
    put32(w + 24, (1u << 2) | (1u << 4) | (1u << 3) | (rateAdapt ? 0u : ((uint32_t)M3_RATE_IDX << 16)));   // DAS, MSDU_CNT=1, DIS_MAT, TX_RATE

    m3tok = (m3tok + 1) & 0x1fff;
    put16(w + 32, (uint16_t)(m3tok | 0x8000));                   // txp.msdu_id[0] | VALID
    put32(w + 40, (uint32_t)(pa + 128));                         // txp.ptr[0].buf0
    put16(w + 44, (uint16_t)(len | 0x8000));                     // len0 | LAST
    membar();

    uint8_t *d = txData.desc.va + txData.head * 16;
    put32(d + 0, (uint32_t)pa);
    put32(d + 8, 0);
    put32(d + 12, 0);
    put32(d + 4, MT_DMA_CTL_SD_LEN0(64) | MT_DMA_CTL_LAST_SEC0);
    txData.head = (txData.head + 1) % txData.n;
    membar();
    wr(txData.regs + Q_CPU_IDX, txData.head);
    for (int us = 0; us < 200000; us += 20) {
        if ((int)(rd(txData.regs + Q_DMA_IDX) & 0xffff) == txData.head) return true;
        IODelay(20);
    }
    m3log("tx stall cpu=%u dma=%u", txData.head, rd(txData.regs + Q_DMA_IDX) & 0xffff);
    return false;
}

// Looks for a management frame (header after the RXD groups) from the AP to us whose fc byte 0 is m3wantFc.
bool itlmt7925::m3RxMgmt(const uint8_t *p, uint32_t len)
{
    uint32_t pt = len >= 8 ? le32(p) >> 27 : 0;
    if ((pt == 0 || pt == 6) && len >= 24 && m3rxOther < 8) {      // TXS / TXRX_NOTIFY: did the firmware transmit our frame?
        m3rxOther++;
        m3log("rx pt=%u len=%u %08x %08x %08x %08x %08x %08x", pt, len, le32(p), le32(p + 4), le32(p + 8), le32(p + 12),
              le32(p + 16), le32(p + 20));
    }
    if (len < 32 + 24 || m3rxLen) return false;
    for (uint32_t off = 32; off + 24 <= len && off < 160; off++) {
        if (p[off] != m3wantFc) continue;
        if (memcmp(p + off + 4, mac, 6) != 0 || memcmp(p + off + 10, tgtBssid, 6) != 0) continue;
        uint32_t n = len - off;
        if (n > sizeof(m3rx)) n = sizeof(m3rx);
        memcpy(m3rx, p + off, n);
        m3rxLen = n;
        return true;
    }
    // Anything else coming from the AP while we wait (not beacons / probe responses): log a few for diagnosis
    static int dumped = 0;
    for (uint32_t off = 32; dumped < 6 && off + 24 <= len && off < 160; off++) {
        if (memcmp(p + off + 10, tgtBssid, 6) != 0 || p[off] == 0x80 || p[off] == 0x50 || p[off + 1] > 0x7f) continue;
        dumped++;
        m3log("other frame off=%u fc=%02x%02x a1=%02x%02x%02x%02x%02x%02x len=%u", off, p[off], p[off + 1], p[off + 4],
              p[off + 5], p[off + 6], p[off + 7], p[off + 8], p[off + 9], len);
        break;
    }
    return false;
}

// Sends `frame` (up to 4 times, 150 ms apart) until a frame with fc byte 0 == wantFc comes back.
bool itlmt7925::m3Xchg(const uint8_t *frame, uint32_t len, uint8_t subtype, uint8_t wantFc, uint32_t waitMs)
{
    m3rxLen = 0;
    m3wantFc = wantFc;
    bool got = false;
    for (int attempt = 1; attempt <= 4 && !got; attempt++) {
        bool ok = m3Tx(frame, len, 0, subtype);
        m3log("tx %02x len=%u #%d %s", frame[0], len, attempt, ok ? "queued" : "FAILED");
        for (uint32_t i = 0; i < waitMs && !m3rxLen; i++) m3Pump(1);
        got = m3rxLen != 0;
    }
    m3wantFc = 0;
    if (!got) m3log("no response fc=%02x rxpk=%u txcpu=%u txdma=%u", wantFc, rxPkts, txData.head, rd(txData.regs + Q_DMA_IDX) & 0xffff);
    return got;
}

static void m3Hdr(uint8_t *h, uint8_t fc0, const uint8_t *bssid, const uint8_t *sa)
{
    bzero(h, 24);
    h[0] = fc0;
    memcpy(h + 4, bssid, 6);
    memcpy(h + 10, sa, 6);
    memcpy(h + 16, bssid, 6);
}

int itlmt7925::m3Auth()
{
    uint8_t f[30];
    m3Hdr(f, 0xb0, tgtBssid, mac);
    put16(f + 24, 0);                                            // open system
    put16(f + 26, 1);                                            // transaction 1
    put16(f + 28, 0);
    if (!m3Xchg(f, sizeof(f), 11, 0xb0, 150)) return ITL_TIMEOUT;
    uint16_t alg = (uint16_t)(m3rx[24] | (m3rx[25] << 8)), seq = (uint16_t)(m3rx[26] | (m3rx[27] << 8)),
             st = (uint16_t)(m3rx[28] | (m3rx[29] << 8));
    m3log("auth resp alg=%u seq=%u status=%u", alg, seq, st);
    return (alg == 0 && seq == 2 && st == 0) ? 0 : ITL_EIO;
}

int itlmt7925::m3Assoc()
{
    uint8_t f[224];
    m3Hdr(f, 0x00, tgtBssid, mac);
    uint8_t *p = f + 24;
    put16(p, 0x0411); p += 2;                                    // ESS | PRIVACY | short slot
    put16(p, 10); p += 2;                                        // listen interval
    uint8_t sl = (uint8_t)strlen(m3ssid);
    *p++ = 0; *p++ = sl; memcpy(p, m3ssid, sl); p += sl;
    static const uint8_t rates[8] = {0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c};
    *p++ = 1; *p++ = 8; memcpy(p, rates, 8); p += 8;
    static const uint8_t rsn[22] = {0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
                                    0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00};
    memcpy(p, rsn, sizeof(rsn)); p += sizeof(rsn);
    if (fastActive) {
        p += itl_caps_build_ht(p);
        if (apCaps.vht) p += itl_caps_build_vht(p);
        static const uint8_t wmm[9] = {0xdd, 0x07, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};   // WMM information element: we are QoS-capable
        memcpy(p, wmm, sizeof(wmm)); p += sizeof(wmm);
    }
    if (!m3Xchg(f, (uint32_t)(p - f), 0, 0x10, 150)) return ITL_TIMEOUT;
    uint16_t st = (uint16_t)(m3rx[26] | (m3rx[27] << 8)), aid = (uint16_t)((m3rx[28] | (m3rx[29] << 8)) & 0x3fff);
    m3log("assoc resp status=%u aid=%u", st, aid);
    if (st) return ITL_EIO;
    m3aid = aid;
    return 0;
}

// ---------------------------------------------------------------------------------------------
// M3 sequence. Each step is added behind its own log line so a partial run is still informative.
// ---------------------------------------------------------------------------------------------
int itlmt7925::m3Run()
{
    m3log("armed");

    // 0. credentials: itl_ssid / itl_psk, then itl_ssid2 / itl_psk2 ... itl_ssid4 / itl_psk4 (the list order is the priority)
    struct Net { char ssid[33]; char psk[72]; } nets[4];
    int nn = 0;
    for (int i = 1; i <= 4; i++) {
        char ks[16], kp[16];
        snprintf(ks, sizeof(ks), i == 1 ? "itl_ssid" : "itl_ssid%d", i);
        snprintf(kp, sizeof(kp), i == 1 ? "itl_psk" : "itl_psk%d", i);
        OSString *s = OSDynamicCast(OSString, getProperty(ks));
        OSString *k = OSDynamicCast(OSString, getProperty(kp));
        if (!s || !k || !s->getLength() || s->getLength() > 32 || k->getLength() < 8 || k->getLength() > 63) continue;
        strlcpy(nets[nn].ssid, s->getCStringNoCopy(), sizeof(nets[nn].ssid));
        strlcpy(nets[nn].psk, k->getCStringNoCopy(), sizeof(nets[nn].psk));
        nn++;
    }
    if (!nn) {
        m3log("no usable itl_ssid/itl_psk in the personality");
        m3Publish("nocred");
        return ITL_EINVAL;
    }
    m3log("credentials: %d network(s)", nn);
    for (int i = 0; i < nn; i++)
        m3log("net%d ssid len=%u psk len=%u", i + 1, (unsigned)strlen(nets[i].ssid), (unsigned)strlen(nets[i].psk));

    // The scan already ran (M2). m3Capture only keeps beacons of the SSID in m3ssid, so look for each configured network in turn
    // (directed probe request) until one is seen; a network that the wildcard scan already listed goes first.
    int order[4], no = 0;
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < nn; i++) {
            bool seen = false;
            for (int j = 0; j < nAps; j++) if (!strcmp(aps[j].ssid, nets[i].ssid)) seen = true;
            if (seen == (pass == 0)) order[no++] = i;
        }
    int sel = -1;
    for (int attempt = 1; attempt <= 3 && sel < 0; attempt++) {
        for (int k = 0; k < no && sel < 0; k++) {
            int i = order[k];
            strlcpy(m3ssid, nets[i].ssid, sizeof(m3ssid));
            tgtFound = false;
            m3log("scan #%d for net%d (directed)", attempt, i + 1);
            m2Scan(m3ssid);
            for (int w = 0; w < 5 && !tgtFound; w++) { rxDataPoll(); IOSleep(100); }
            if (tgtFound) sel = i;
        }
    }
    m3Publish("scan");
    if (sel < 0) { m3log("no configured network seen in the scan"); return ITL_EIO; }
    strlcpy(m3ssid, nets[sel].ssid, sizeof(m3ssid));
    strlcpy(m3psk, nets[sel].psk, sizeof(m3psk));
    bzero(nets, sizeof(nets));
    m3log("selected net%d ssid len=%u", sel + 1, (unsigned)strlen(m3ssid));
    m3log("target %02x:%02x:%02x:%02x:%02x:%02x ch=%u cap=%04x bi=%u ies=%u", tgtBssid[0], tgtBssid[1], tgtBssid[2],
          tgtBssid[3], tgtBssid[4], tgtBssid[5], tgtCh, tgtCap, tgtBcnInt, tgtIeLen);

    // PMK now (4096 PBKDF2 iterations), kept for the 4-way handshake
    uint8_t pmk[32];
    itl_wpa_pmk(m3psk, (const uint8_t *)m3ssid, strlen(m3ssid), pmk);
    memcpy(m3pmk, pmk, sizeof(m3pmk));
    bzero(pmk, sizeof(pmk));
    m3log("pmk ready");
    m3Publish("pmk");

    // step 2: firmware / hardware prep, BSS + STA records in the "not associated" state, ROC on the AP's channel
    m3ParseAp();
    m3HwPrep();
    m3Bss(false);
    m3Sta(0, true, 0);
    m3Roc(true);
    for (int i = 0; i < 1000 && !rocGrant; i++) m3Pump(1);
    m3log("roc grant=%d status=%u", rocGrant ? 1 : 0, rocStatus);
    if (!rocGrant || rocStatus != 0) { m3Publish("roc-fail"); m3Roc(false); return ITL_EIO; }

    // step 3: open-system authentication and association, both on the channel held by the ROC
    m3log("sta mac %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    int rc = m3Auth();
    m3Publish(rc ? "auth-fail" : "auth-ok");
    if (!rc) {
        rc = m3Assoc();
        m3Publish(rc ? "assoc-fail" : "assoc-ok");
    }

    // step 4: BSS up, station state "associated", 4-way handshake with key install, Ethernet interface
    if (!rc) {
        m3Bss(true);
        m3Sta(2, false, m3aid);
        rc = m3Handshake();
        m3wantEapol = false;
        m3Publish(rc ? "hs-fail" : "hs-ok");
    }
    m3Roc(false);
    bzero(m3pmk, sizeof(m3pmk));
    if (!rc) {
        m3log("RESULT rc=0 link up");
        m3Link();                                            // never returns
    }
    return rc;
}

// ---------------------------------------------------------------------------------------------
// Step 4: data frames, 4-way handshake, keys, link
// ---------------------------------------------------------------------------------------------
static inline uint16_t be16p(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

// First few interesting packets (ARP, UDP incl. DHCP, ICMP) in either direction, to see how far traffic gets.
void itlmt7925::m3Note(const char *dir, const uint8_t *e, uint32_t len)
{
    if (stNote >= 24 || len < 34) return;
    uint16_t et = be16p(e + 12);
    if (et == 0x0806) {
        stNote++;
        m3log("%s arp op=%u", dir, e[21]);
    } else if (et == 0x0800) {
        const uint8_t *ip = e + 14;
        uint32_t ihl = (ip[0] & 15) * 4;
        if (ip[9] == 17 && len >= 14 + ihl + 8) {
            stNote++;
            m3log("%s udp %u>%u len=%u", dir, be16p(ip + ihl), be16p(ip + ihl + 2), len);
        } else if (ip[9] == 1 && len > 14 + ihl) {
            stNote++;
            m3log("%s icmp type=%u", dir, ip[ihl]);
        }
    }
}

// RX data frame from the AP (802.11 header + LLC/SNAP after the RXD groups; header translation is off, decryption is done by
// the hardware once the keys are installed). EAPOL goes to the handshake, everything else to the Ethernet interface.
bool itlmt7925::m3RxData(const uint8_t *p, uint32_t len)
{
    for (uint32_t off = 32; off + 24 <= len && off < 120; off++) {
        uint8_t fc0 = p[off], fc1 = p[off + 1];
        if ((fc0 & 0x0c) != 0x08 || (fc1 & 3) != 2) continue;               // data frame, From DS
        if (memcmp(p + off + 10, tgtBssid, 6) != 0) continue;
        bool group = p[off + 4] & 1;
        if (!group && memcmp(p + off + 4, mac, 6) != 0) continue;
        if (fc0 & 0x40) return true;                                         // null / QoS null: no payload
        uint32_t hl = (fc0 & 0x80) ? 26 : 24;
        static const uint8_t llc[6] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};
        for (uint32_t g = 0; g <= 12 && off + hl + g + 8 <= len; g++) {
            const uint8_t *l = p + off + hl + g;
            if (memcmp(l, llc, 6) != 0) continue;
            uint16_t et = be16p(l + 6);
            const uint8_t *pl = l + 8;
            uint32_t pn = len - (uint32_t)(pl - p);
            stRxData++;
            if (et == 0x888e) {
                stRxEapol++;
                if (m3wantEapol && !m3eapolLen) {
                    uint32_t n = pn > sizeof(m3eapol) ? (uint32_t)sizeof(m3eapol) : pn;
                    memcpy(m3eapol, pl, n);
                    m3eapolLen = n;
                }
                return true;
            }
            if (!m3linkUp || pn > 1600) { stRxDrop++; return true; }
            if (memcmp(p + off + 16, mac, 6) == 0) return true;             // our own broadcast echoed back
            static uint8_t eb[14 + 1600];
            memcpy(eb, p + off + 4, 6);                                      // DA
            memcpy(eb + 6, p + off + 16, 6);                                 // SA = addr3
            eb[12] = (uint8_t)(et >> 8); eb[13] = (uint8_t)et;
            memcpy(eb + 14, pl, pn);
            stRxEth++;
            m3Note("rx", eb, 14 + pn);
            ethRx(eb, 14 + pn);
            return true;
        }
        return false;
    }
    return false;
}

bool itlmt7925::m3TxData(const uint8_t *da, uint16_t ethertype, const uint8_t *payload, uint32_t plen, bool protect)
{
    uint8_t f[34 + 1560];
    if (plen > 1560) return false;
    // With HT/VHT negotiated, unicast data (not the EAPOL handshake) goes out as QoS data (TID 0) at the rate the firmware
    // chooses; group-addressed frames and everything else keep the fixed basic rate.
    bool ra = fastActive && m3linkUp && ethertype != 0x888e && !(da[0] & 1);
    uint32_t hl = ra ? 26 : 24;
    bzero(f, hl);
    f[0] = ra ? 0x88 : 0x08;                                 // data / QoS data
    f[1] = protect ? 0x41 : 0x01;                            // To DS (+ Protected: the hardware adds the CCMP header)
    memcpy(f + 4, tgtBssid, 6);
    memcpy(f + 10, mac, 6);
    memcpy(f + 16, da, 6);                                   // QoS control (bytes 24-25) stays 0: TID 0, normal ack
    static const uint8_t llc[6] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};
    memcpy(f + hl, llc, 6);
    f[hl + 6] = (uint8_t)(ethertype >> 8); f[hl + 7] = (uint8_t)ethertype;
    memcpy(f + hl + 8, payload, plen);
    return m3Tx(f, hl + 8 + plen, 2, ra ? 8 : 0, protect, ra);
}

// Ethernet frame from the network stack
bool itlmt7925::ethTx(const uint8_t *e, uint32_t len)
{
    if (!m3linkUp || len < 14) return false;
    bool ok = m3TxData(e, be16p(e + 12), e + 14, len - 14, true);
    if (ok) stTx++; else stTxFail++;
    m3Note("tx", e, len);
    return ok;
}

// STA_REC_KEY_V3 (mt7925_mcu_sta_key_tlv): CCMP key on the AP's wcid (pairwise) or on the BSS's broadcast wcid (group)
int itlmt7925::m3Key(uint16_t wcid, bool pairwise, uint8_t keyId, const uint8_t *key, uint8_t keyLen)
{
    M3Msg m;
    m.init(8);
    m.b[0] = M3_BSS_IDX;
    m.b[1] = wcid & 0xff;
    m.b[4] = 1;                                              // is_tlv_append
    m.b[5] = pairwise ? 0 : 0xe;                             // muar_idx: omac for a station, 0xe for the broadcast wcid
    m.b[6] = wcid >> 8;
    uint8_t *s = m.tlv(0x27, 68);                            // STA_REC_KEY_V3
    s[4] = 1;                                                // add
    s[5] = pairwise ? 1 : 0;                                 // tx_key
    s[6] = pairwise ? 1 : 0;                                 // key_type
    s[7] = 0;                                                // is_authenticator
    memcpy(s + 8, tgtBssid, 6);                              // peer_addr
    s[14] = M3_BSS_IDX;
    s[15] = 4;                                               // CONNAC3_CIPHER_AES_CCMP
    s[16] = keyId;
    s[17] = keyLen;
    s[18] = wcid & 0xff;
    s[19] = 1;                                               // mgmt_prot
    memcpy(s + 20, key, keyLen);
    int rc = m3Cmd(UNI_STA_REC_UPDATE, m.b, m.n, pairwise ? "key(ptk)" : "key(gtk)");
    bzero(m.b, sizeof(m.b));
    return rc;
}

int itlmt7925::m3Handshake()
{
    static const uint8_t rsnIe[22] = {0x30, 0x14, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
                                      0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00};
    uint8_t snonce[32], zero[32], replay[8], out[160], kd[8 * 40];
    itl_eapol_key k;
    bzero(zero, sizeof(zero));
    read_random(snonce, sizeof(snonce));
    m3dataOn = true;
    m3eapolLen = 0;
    m3wantEapol = true;

    for (uint32_t i = 0; i < 6000 && !m3eapolLen; i++) m3Pump(1);
    if (!m3eapolLen) { m3log("eapol: no msg1 (rxdata=%u rxpk=%u)", stRxData, rxPkts); return ITL_TIMEOUT; }
    if (itl_eapol_parse(m3eapol, m3eapolLen, &k) || !(k.info & ITL_KI_ACK) || (k.info & ITL_KI_MIC)) {
        m3log("eapol: bad msg1 len=%u", m3eapolLen);
        return ITL_EIO;
    }
    m3log("eapol m1 info=%04x len=%u", k.info, m3eapolLen);
    memcpy(replay, k.replay, 8);
    itl_ptk ptk;
    itl_wpa_ptk(m3pmk, tgtBssid, mac, k.nonce, snonce, &ptk);

    size_t n2 = itl_eapol_build(out, 0x010a, 0, replay, snonce, rsnIe, sizeof(rsnIe));       // ver 2 | pairwise | MIC
    itl_eapol_mic(ptk.kck, out, n2);

    bool got3 = false;
    for (int attempt = 1; attempt <= 4 && !got3; attempt++) {
        m3eapolLen = 0;
        bool ok = m3TxData(tgtBssid, 0x888e, out, (uint32_t)n2, false);
        m3log("eapol m2 tx #%d %s", attempt, ok ? "queued" : "FAILED");
        for (uint32_t i = 0; i < 1500 && !got3; i++) {
            m3Pump(1);
            if (!m3eapolLen) continue;
            if (itl_eapol_parse(m3eapol, m3eapolLen, &k) == 0 && (k.info & ITL_KI_MIC) && (k.info & ITL_KI_INSTALL)) got3 = true;
            else m3eapolLen = 0;                              // a repeated msg1: wait on, the AP will still send msg3
        }
    }
    if (!got3) { m3log("eapol: no msg3"); return ITL_TIMEOUT; }
    uint32_t elen = 4u + be16p(m3eapol + 2);
    if (!itl_eapol_mic_ok(ptk.kck, m3eapol, elen)) { m3log("eapol: msg3 MIC bad"); return ITL_EIO; }
    m3log("eapol m3 info=%04x mic ok datalen=%u", k.info, k.datalen);

    // key data: wrapped RSN IE + GTK KDE
    uint8_t gtk[32], gtkId = 0, gtkLen = 0;
    if ((k.info & ITL_KI_ENCRYPTED) && k.datalen >= 24 && k.datalen <= sizeof(kd) + 8 &&
        itl_aes128_unwrap(ptk.kek, k.data, k.datalen, kd) == 0) {
        const uint8_t *e = kd, *end = kd + k.datalen - 8;
        while (e + 2 <= end && e + 2 + e[1] <= end) {
            if (e[0] == 0xdd && e[1] >= 22 && e[2] == 0x00 && e[3] == 0x0f && e[4] == 0xac && e[5] == 1) {
                gtkLen = (uint8_t)(e[1] - 6);
                if (gtkLen > sizeof(gtk)) gtkLen = 0;
                else { gtkId = e[6] & 3; memcpy(gtk, e + 8, gtkLen); }
                break;
            }
            e += 2 + e[1];
        }
    } else {
        m3log("eapol: key data not usable (info=%04x len=%u)", k.info, k.datalen);
    }
    m3log("gtk id=%u len=%u", gtkId, gtkLen);
    if (gtkLen != 16) return ITL_EIO;

    // msg4: ver 2 | pairwise | MIC | secure
    uint8_t replay3[8];
    memcpy(replay3, k.replay, 8);
    size_t n4 = itl_eapol_build(out, 0x030a, 0, replay3, zero, nullptr, 0);
    itl_eapol_mic(ptk.kck, out, n4);
    bool ok = m3TxData(tgtBssid, 0x888e, out, (uint32_t)n4, false);
    m3log("eapol m4 tx %s", ok ? "queued" : "FAILED");
    for (int i = 0; i < 30; i++) m3Pump(1);                    // let it go out in the clear before the keys arrive

    m3cipher = 4;
    m3Bss(true);                                               // BSS now secured with CCMP
    int r1 = m3Key(M3_AP_WCID, true, 0, ptk.tk, 16);
    int r2 = m3Key(M3_BMC_WCID, false, gtkId, gtk, 16);
    memcpy(m3ptk, &ptk, sizeof(m3ptk));
    bzero(&ptk, sizeof(ptk));
    bzero(gtk, sizeof(gtk));
    bzero(kd, sizeof(kd));
    return (r1 || r2) ? ITL_EIO : 0;
}

// Link is up: expose the Ethernet interface and keep servicing the receive rings forever (this thread never returns).
void itlmt7925::m3Link()
{
    bool eok = ethCreate();
    m3log("ethernet interface %s", eok ? "created" : "FAILED");
    m3linkUp = true;
    m3sleepy = true;
    uint64_t t0 = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &t0);
    uint64_t lastStat = 0;
    for (;;) {
        m3Pump(1);
        uint64_t now = 0;
        absolutetime_to_nanoseconds(mach_absolute_time(), &now);
        uint64_t up = (now - t0) / 1000000000ull;
        if (up >= lastStat + (up < 300 ? 5 : 60)) {
            lastStat = up;
            char st[200];
            snprintf(st, sizeof(st), "up=%llus rxd=%u eapol=%u eth=%u drop=%u tx=%u fail=%u rxpk=%u", up, stRxData, stRxEapol,
                     stRxEth, stRxDrop, stTx, stTxFail, rxPkts);
            nvramStat(st);
            nvramChunks(m3buf);
        }
    }
}
