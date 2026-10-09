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
    return 0;
}
