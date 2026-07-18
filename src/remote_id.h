/* -*- tab-width: 2; mode: c; -*-
 *
 * A program for the nRF52840 to transmit ASTM F3411/ASD-STAN 4709-002/opendroneid 
 * remote identification signals over Bluetooth.
 * 
 * Copyright (c) 2023 Steve Jack
 *
 * Apache 2.0 licence
 *
 * Parts of this program are taken from samples which are
 * Copyright (c) Intel Corporation 
 * or
 * Copyright (c) Nordic Semiconductor
 *
 */

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>

#include "opendroneid.h"

                        // 01234567890123456789
#define UAV_OPERATOR      "GBR-OP-ABCD12345678"
#define UAV_ID            "ZZZZH1234567"             // ~ => random generated
#define SELF_ID           "CAA UAS 7068"
#define UA_TYPE           ODID_UATYPE_NONE

#define EU_CATEGORY       ODID_CATEGORY_EU_SPECIFIC

#define AUTH_KEY          "0123456789abcdef"
#define AUTH_IV           "nopqrs"

// Added by Sandoche
#define DET_IPV6_BYTES    { 0x20,0x01,0x00,0x30,0x3f,0xf8,0x04,0x02,0x73,0xc6,0x58,0x1c,0xcc,0xde,0x39,0x98,0x00,0x00,0x00,0x00 }


#define GPS_PASSTHROUGH   1
#define TXT_BOTH_PORTS    0 // Sends error messages to both the serial and USB ports.
#define SPEKTRUM          0
#define REQ_SATS          8 // 8 for real use, 5 for indoor testing.
#define BATT_VOLTS        0
#define STATIC_BLE_MAC    1

// Unix epoch (seconds) at build time, injected by CMake. The board has no RTC, so this is the
// transmitter's real-time base; add uptime to approximate the current time.
#define BUILD_UNIX_TIME   1784382398UL

// National/regional variations.
#define ID_JAPAN          0
#define ID_USA            0

//

void      txt_message(const char *,const int,const int,const int); // Packages the message into a NMEA TXT.
void      debug_message(const char *);
float     battery_voltage(void);
int       crypto_init(uint8_t *);
#if ID_JAPAN
int       auth_japan(ODID_UAS_Data *,ODID_Message_encoded *,uint8_t *);
#endif

#ifdef __cplusplus

void      txt_message(const char *);

extern "C" {
#endif

  uint64_t  alt_unix_secs(int,int,int,int,int,int);

#ifdef __cplusplus
};
#endif

/*
 *
 */

