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

#pragma GCC diagnostic warning "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wparentheses"
// #pragma GCC diagnostic ignored "-Wunused-but-set-variable"

#include "remote_id.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>

// #include <zephyr/random/rand32.h>

#include "gps2.h"
#include "odid_bt.h"
#if SPEKTRUM
#include "spektrum_i2c.h"
#endif

#include <psa/crypto.h>

//

static const char                 uav_operator[] = UAV_OPERATOR,
                                  self_id[]      = SELF_ID,
                                 *program_name   = "Remote ID";
static char                       uav_id[25]     = UAV_ID;
static const ODID_uatype_t        ua_type        = UA_TYPE;
static const ODID_category_EU_t   category       = EU_CATEGORY;

//

static GPS2                       gps;
static RID_open                   squitter;
#if SPEKTRUM
static Spektrum_I2C               spektrum;
#endif

static ODID_UAS_Data              UAS_data;
const uint64_t                    odid_datum     = 1546300800;

static const struct device       *usb_ser_dev    = NULL,
                                 *uart_dev       = DEVICE_DT_GET(DT_NODELABEL(uart0));
#if defined(CONFIG_BOARD_NRF52840DONGLE_NRF52840)
const char                        config_board[] = "nrf52840 dongle";
static const struct gpio_dt_spec  led_green      = GPIO_DT_SPEC_GET(DT_ALIAS(led0_green),gpios);
static const struct gpio_dt_spec  led_red        = GPIO_DT_SPEC_GET(DT_ALIAS(led1_red),gpios);
static const struct gpio_dt_spec  led_green1     = GPIO_DT_SPEC_GET(DT_ALIAS(led1_green),gpios);
static const struct gpio_dt_spec  led_blue       = GPIO_DT_SPEC_GET(DT_ALIAS(led1_blue),gpios);
#else
const char                        config_board[] = "nrf52";
static const struct gpio_dt_spec  led_red        = GPIO_DT_SPEC_GET(DT_ALIAS(led0),gpios);
static const struct gpio_dt_spec  led_green      = GPIO_DT_SPEC_GET(DT_ALIAS(led1),gpios);
static const struct gpio_dt_spec  led_blue       = GPIO_DT_SPEC_GET(DT_ALIAS(led2),gpios);
#endif

#if 0
static int                        time_set = 0;
#endif
static int                        base_set = 0, build_report = 0;

static void init_odid(const char *,char *,const char *);
static void status_leds(uint32_t,uint32_t,int,int);

// Clé privée Ed25519 (32 bytes)
static const uint8_t drone_private_key = {
    0x1a, 0xf7, 0x97, 0xa4, 0x85, 0xbc, 0x36, 0x47,
    0x45, 0x6d, 0xe5, 0x3f, 0xf4, 0x11, 0x92, 0xc5,
    0x8e, 0x88, 0x1a, 0x7a, 0xf2, 0x36, 0x36, 0xa6,
    0x2a, 0x2d, 0x76, 0x39, 0xcf, 0xb6, 0x07, 0xc0
};

// Clé publique Ed25519 (32 bytes)
static const uint8_t drone_public_key = {
    0x37, 0x04, 0x92, 0x23, 0x96, 0xfb, 0x7e, 0x95,
    0x4b, 0x73, 0x01, 0x05, 0xdc, 0x77, 0x95, 0x2d,
    0x04, 0xc0, 0x21, 0x1b, 0x80, 0xd6, 0xcb, 0xb5,
    0x65, 0x56, 0xb4, 0xb0, 0xd7, 0x7b, 0x74, 0x35
};

// DET de la HDA parente
static const uint8_t auth_hda_det[16] = {
    0x20, 0x01, 0x00, 0x3e, 0xfe, 0x04, 0x02, 0x05,
    0x06, 0x37, 0xac, 0x6f, 0x16, 0x7a, 0x23, 0x03
};

// DET du drone
static const uint8_t drone_det = {
    0x20, 0x01, 0x00, 0x30, 0x3f, 0xf8, 0x04, 0x02,
    0x78, 0x7f, 0x84, 0xb0, 0x6b, 0xa2, 0x17, 0x64
};

static void build_auth_link_message(ODID_UAS_Data *uas) {

    // Timestamps depuis epoch DRIP
    uint32_t now = (uint32_t)(k_uptime_get() / 1000) + 28000000;
    uint32_t vnb = now - 60;
    uint32_t vna = now + 3600;

    // Message à signer : VNB(4) | VNA(4) | childDET(16) | childHI(32) | parentDET(16) = 72 bytes
    uint8_t message[72];
    memset(message, 0, sizeof(message));

    message[0] = vnb & 0xFF;
    message[1] = (vnb >> 8) & 0xFF;
    message[2] = (vnb >> 16) & 0xFF;
    message[3] = (vnb >> 24) & 0xFF;

    message[4] = vna & 0xFF;
    message[5] = (vna >> 8) & 0xFF;
    message[6] = (vna >> 16) & 0xFF;
    message[7] = (vna >> 24) & 0xFF;

    memcpy(message + 8,  drone_det,        16); // childDET
    memcpy(message + 24, drone_public_key,  32); // childHI
    memcpy(message + 56, auth_hda_det,     16); // parentDET

    // Signer avec PSA Crypto
    psa_status_t         status;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_key_id_t         key_id;
    uint8_t              signature[64];
    size_t               sig_len = 0;

    // 1. Initialisation avec vérification du statut (Crucial pour capter les erreurs mémoire)
    status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        printk("PSA crypto init failed: %d\n", status);
        return;
    }

    // 2. Configuration des attributs de la clé (256 bits pour Ed25519)
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_PURE_EDDSA);
    psa_set_key_type(&attributes,
        PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_TWISTED_EDWARDS));
    psa_set_key_bits(&attributes, 255);

    // 3. Importation de la clé privée
    status = psa_import_key(&attributes, drone_private_key, 32, &key_id);
    if (status != PSA_SUCCESS) {
        printk("PSA import key failed: %d\n", status);
        return;
    }

    // 4. Signature du message
    status = psa_sign_message(key_id, PSA_ALG_PURE_EDDSA,
                              message, sizeof(message),
                              signature, sizeof(signature), &sig_len);
    
    // 5. Destruction de la clé en RAM
    psa_destroy_key(key_id);

    if (status != PSA_SUCCESS) {
        printk("PSA sign failed: %d\n", status);
        return;
    }

    printk("Signature Ed25519 OK (%d bytes)\n", (int)sig_len);

    // Payload complet : SAM type(1) | message(72) | signature(64) = 137 bytes
    uint8_t auth_payload[137];
    auth_payload[0] = 0x01; // SAM type = Link
    memcpy(auth_payload + 1,  message,   72);
    memcpy(auth_payload + 73, signature, 64);

    // Page 0 : 17 bytes de données
    uas->Auth[0].AuthType      = ODID_AUTH_SPECIFIC_AUTHENTICATION;
    uas->Auth[0].DataPage      = 0;
    uas->Auth[0].LastPageIndex = 5;
    uas->Auth[0].Length        = 137;
    uas->Auth[0].Timestamp     = now;
    memcpy(uas->Auth[0].AuthData, auth_payload, 17);

    // Pages 1 à 5 : 23 bytes chacune
    for (int page = 1; page <= 5; page++) {
        int offset    = 17 + (page - 1) * 23;
        int remaining = (int)sizeof(auth_payload) - offset;
        int to_copy   = (remaining > 23) ? 23 : remaining;
        if (to_copy <= 0) break;

        uas->Auth[page].AuthType = ODID_AUTH_SPECIFIC_AUTHENTICATION;
        uas->Auth[page].DataPage = page;
        memcpy(uas->Auth[page].AuthData, auth_payload + offset, to_copy);
    }
}

/*
 *
 */

int main(void) {
  struct bt_le_ext_adv_info ext_adv_info;

  // init data
  init_odid(uav_operator, uav_id, self_id);

  // bluetooth init
  squitter.begin(&led_blue, uav_operator, &UAS_data, &ext_adv_info);

  printk("\n*** EMULATEUR REMOTE ID PRET ***\n");
  printk("Programme: %s\n", program_name);
  printk("Statut: Emission en cours...\n");
  
  // loop
  while (1) {
    // update UAS data
    squitter.foreground(&UAS_data);

    // cpu sleep
    k_sleep(K_MSEC(100));

    // blinking led
    status_leds(k_uptime_get_32(), k_uptime_get_32(), 10, 1);
  }

  return 0;
}
/*
 *
 */

void txt_message(const char *text) {
  printk("[MSG] %s\n", text);
}

void txt_message(const char *text, const int message, const int messages, const int severity) {
  printk("[MSG] %s\n", text);
}

/*
 *
 */

void debug_message(const char *message) {
  if (!usb_ser_dev) return;

  while (*message) {
    uart_poll_out(usb_ser_dev,*message++);
  }
  
  return;
}

/*
 *
 */

static void init_odid(const char *uav_op,char *uav,const char *self) {

  uint32_t            sn1, sn2;
  ODID_Location_data *location;
  ODID_System_data   *system;

  memset(&UAS_data,0,sizeof(UAS_data));

  location = &UAS_data.Location;
  system   = &UAS_data.System;

  //

  //if (uav[0] == '~') {

  //  sn1 = sys_rand32_get() % 1000000;
  //  sn2 = sys_rand32_get() % 1000000;

  //  sprintf(uav,"ZZZZL%06u%06u",sn1,sn2);
  //}

  //

  UAS_data.BasicID[0].UAType =
  UAS_data.BasicID[1].UAType = ua_type;

// --- DET IPV6 injection ---
static const unsigned char ipv6_det[] = {
    0x20, 0x01, 0x00, 0x30, 0x3f, 0xf8, 0x04, 0x02,
    0x78, 0x7f, 0x84, 0xb0, 0x6b, 0xa2, 0x17, 0x64,
    0x00, 0x00, 0x00, 0x00
};

UAS_data.BasicID[0].IDType = ODID_IDTYPE_SPECIFIC_SESSION_ID;
memcpy(UAS_data.BasicID[0].UASID, ipv6_det, sizeof(ipv6_det));

UAS_data.BasicID[1].IDType = ODID_IDTYPE_SPECIFIC_SESSION_ID;
memcpy(UAS_data.BasicID[1].UASID, ipv6_det, sizeof(ipv6_det));
// --- FIN INJECTION ---
printk("\n=== DEMARRAGE DU DRONE ===\n");
printk("DET (IPv6) injecté avec succes !\n");
printk("==========================\n");

  // --- DRONE'S COORDINATES ---
  location->Status         = ODID_STATUS_AIRBORNE; // Le drone est déclaré "en vol"
  location->Latitude       = 48.6248;              // Latitude (ex: Tour Eiffel)
  location->Longitude      = 2.4449;               // Longitude
  location->AltitudeGeo    = 150.0;                // Altitude Géodésique (mètres)
  location->AltitudeBaro   = 150.0;                // Altitude Barométrique
  location->Height         = 50.0;                 // Hauteur par rapport au sol
  location->Direction      = 180.0;                // Cap (0-360°)
  location->SpeedHorizontal = 10.5;                // Vitesse horizontale (m/s)
  location->SpeedVertical   = 1.0;                 // Vitesse verticale (m/s)
  
  location->HorizAccuracy  = ODID_HOR_ACC_10_METER;
  location->VertAccuracy   = ODID_VER_ACC_10_METER;
  location->BaroAccuracy   = ODID_VER_ACC_10_METER;
  location->SpeedAccuracy  = ODID_SPEED_ACC_10_METERS_PER_SECOND;
  location->TSAccuracy     = ODID_TIME_ACC_0_1_SECOND;
  location->TimeStamp      = 30.0;                 // Secondes depuis l'heure pile

  location->HeightType         = ODID_HEIGHT_REF_OVER_TAKEOFF;

 // --- OPERATOR's COORDINATES ---
  system->OperatorLocationType = ODID_OPERATOR_LOCATION_TYPE_TAKEOFF;
  system->OperatorLatitude     = 48.6251;          // Position du pilote
  system->OperatorLongitude    = 2.4422;
  system->OperatorAltitudeGeo  = 35.0;             // Altitude sol au décollage
  system->AreaCount            = 1;
  system->AreaRadius           = 0;                // Pas de zone de vol définie
  system->ClassificationType   = ODID_CLASSIFICATION_TYPE_EU;
  system->CategoryEU           = ODID_CATEGORY_EU_OPEN;
  system->ClassEU              = ODID_CLASS_EU_CLASS_1;

  system->AreaCeiling          =
  system->AreaFloor            = -1000.0;

#if ! (ID_USA || ID_JAPAN)
  if (category) {
    system->ClassificationType = ODID_CLASSIFICATION_TYPE_EU;
    system->CategoryEU         = category;
    system->ClassEU            = ODID_CLASS_EU_UNDECLARED;
  }
#endif

// --- OPERATOR ID ---
UAS_data.OperatorID.OperatorIdType = ODID_OPERATOR_ID;
strncpy(UAS_data.OperatorID.OperatorId, "200100303ff804020191ab23cd45ef67", ODID_ID_SIZE);

// --- SELF ID ---
UAS_data.SelfID.DescType   = ODID_DESC_TYPE_TEXT;
strncpy(UAS_data.SelfID.Desc, "DRIP CASSIOPEE", ODID_STR_SIZE);

  // Construire et signer le message Auth Link
  build_auth_link_message(&UAS_data);

  return;
}

/*
 *
 */

// #define STATUS_LED DT_N_ALIAS_statusled
 
 void status_leds(uint32_t uptime,uint32_t last_gps,int sats,int fix) {

  static int                 phase = -1;
  static uint32_t            last_update = 0;
#if defined(STATUS_LED)
  static struct gpio_dt_spec external_led = GPIO_DT_SPEC_GET(DT_ALIAS(statusled),gpios);
#endif

  if (phase < 0) {

    gpio_pin_configure_dt(&led_green,GPIO_OUTPUT_ACTIVE);
    gpio_pin_set_dt(&led_green,0);
#if defined(STATUS_LED)
    gpio_pin_configure_dt(&external_led,GPIO_OUTPUT_ACTIVE);
    gpio_pin_set_dt(&external_led,0);
#endif
    phase = 0;
  }

  if ((uptime - last_update) < 100) {
    return;
  }

  last_update = uptime;

  switch (phase) {

  case 0:
    if ((uptime - last_gps) < 2000) {
      gpio_pin_set_dt(&led_green,1);
#if defined(STATUS_LED)
      gpio_pin_set_dt(&external_led,1);
#endif
    }
    break;

  case  6:
  case  8:
  case 10:
  case 12:
  case 14:
    gpio_pin_set_dt(&led_green,0);
#if defined(STATUS_LED)
      gpio_pin_set_dt(&external_led,0);
#endif
    break;

  case  9:
    if (sats > 3) {
      gpio_pin_set_dt(&led_green,1);
#if defined(STATUS_LED)
      gpio_pin_set_dt(&external_led,1);
#endif
    }
    break;

  case 11:
    if (sats > 9) {
      gpio_pin_set_dt(&led_green,1);
#if defined(DT_N_ALIAS_statusled)
      gpio_pin_set_dt(&external_led,1);
#endif
    }
    break;
  }

  if (++phase > 19) {
    phase = 0;
  }

  return;
}
 
/*
 *
 */

