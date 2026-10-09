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
        IODelay(1000);
    }
}

// Unsolicited firmware events we care about.
void itlmt7925::m3Event(const uint8_t *ev, uint32_t len)
{
    if (!m3On || len < 55) return;
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
    if (apRsnLen >= 14) {
        // version(2) group(4) pairwise count(2) ...
        const uint8_t *r = apRsn + 2;
        uint16_t pc = (uint16_t)(r[6] | (r[7] << 8));
        m3log("rsn group=%02x pairwise n=%u first=%02x akm1=%02x", r[5], pc, pc ? r[11] : 0,
              (apRsnLen >= 12 + 4 * pc + 6) ? r[8 + 4 * pc + 2 + 3] : 0);
    }
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

    uint8_t *p = m.tlv(0x15, 12);                            // STA_REC_PHY
    put16(p + 4, apBasic);                                   // basic_rate
    p[6] = 1u << 3;                                          // PHY_TYPE_BIT_OFDM

    uint8_t *ra = m.tlv(1, 16);                              // STA_REC_RA
    put16(ra + 4, (uint16_t)apSupp << 6);                    // legacy: RA_LEGACY_OFDM GENMASK(13, 6)

    uint8_t *st = m.tlv(7, 16);                              // STA_REC_STATE
    st[4] = state;

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
        put32(t + 16, 1000);                                 // maxinterval (ms)
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
// M3 sequence. Each step is added behind its own log line so a partial run is still informative.
// ---------------------------------------------------------------------------------------------
int itlmt7925::m3Run()
{
    m3log("armed");

    // 0. credentials
    OSString *s = OSDynamicCast(OSString, getProperty("itl_ssid"));
    OSString *k = OSDynamicCast(OSString, getProperty("itl_psk"));
    if (!s || !k || !s->getLength() || s->getLength() > 32 || k->getLength() < 8 || k->getLength() > 63) {
        m3log("no usable itl_ssid/itl_psk in the personality (ssid len=%u psk len=%u)", s ? s->getLength() : 0,
              k ? k->getLength() : 0);
        m3Publish("nocred");
        return ITL_EINVAL;
    }
    strlcpy(m3ssid, s->getCStringNoCopy(), sizeof(m3ssid));
    strlcpy(m3psk, k->getCStringNoCopy(), sizeof(m3psk));
    m3log("credentials: ssid len=%u psk len=%u", (unsigned)strlen(m3ssid), (unsigned)strlen(m3psk));

    // The scan already ran (M2). Anything captured so far came from it; if the target is missing there is nothing to join.
    for (int i = 0; i < 20 && !tgtFound; i++) { rxDataPoll(); IOSleep(100); }
    m3Publish("scan");
    if (!tgtFound) { m3log("target ssid not seen in the scan"); return ITL_EIO; }
    m3log("target %02x:%02x:%02x:%02x:%02x:%02x ch=%u cap=%04x bi=%u ies=%u", tgtBssid[0], tgtBssid[1], tgtBssid[2],
          tgtBssid[3], tgtBssid[4], tgtBssid[5], tgtCh, tgtCap, tgtBcnInt, tgtIeLen);

    // PMK now (4096 PBKDF2 iterations), kept for the 4-way handshake
    uint8_t pmk[32];
    itl_wpa_pmk(m3psk, (const uint8_t *)m3ssid, strlen(m3ssid), pmk);
    m3log("pmk ready");
    bzero(pmk, sizeof(pmk));
    m3Publish("pmk");

    // step 2: firmware / hardware prep, BSS + STA records in the "not associated" state, ROC on the AP's channel
    m3ParseAp();
    m3HwPrep();
    m3Bss(false);
    m3Sta(0, true, 0);
    m3Roc(true);
    m3Pump(2000);
    m3log("roc grant=%d status=%u", rocGrant ? 1 : 0, rocStatus);
    m3Publish(rocGrant && rocStatus == 0 ? "roc-ok" : "roc-fail");
    m3Roc(false);
    return 0;
}
