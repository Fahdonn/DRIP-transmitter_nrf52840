#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <string.h>
#include "opendroneid.h"

// Définition de ton IPv6 brute (16 octets)
static const uint8_t ipv6_det = {
    0x20, 0x01, 0x00, 0x30, 0x3f, 0xf8, 0x04, 0x02, 
    0x01, 0x91, 0xab, 0x23, 0xcd, 0x45, 0xef, 0x67
};

static void bt_ready(int err)
{
    if (err) {
        printk("Erreur d'initialisation Bluetooth (err %d)\n", err);
        return;
    }
    printk("Bluetooth initialisé.\n");

    // 1. Préparation des données Basic ID
    ODID_BasicID_data basic_id;
    memset(&basic_id, 0, sizeof(ODID_BasicID_data));
    
    basic_id.UAType = ODID_UATYPE_HELICOPTER_OR_MULTIROTOR; // Type de drone (à adapter si besoin)
    basic_id.IDType = ODID_IDTYPE_SPECIFIC_SESSION_ID;      // Type 4 : Session ID spécifique (idéal pour IPv6/UUID)
    
    // Copie de ton IPv6 dans le champ UASID (qui fait 20 octets max)
    memcpy(basic_id.UASID, ipv6_det, sizeof(ipv6_det));

    // 2. Encodage du message OpenDroneID
    ODID_BasicID_encoded encoded_basic_id;
    memset(&encoded_basic_id, 0, sizeof(ODID_BasicID_encoded));
    encodeBasicIDMessage(&encoded_basic_id, &basic_id);

    // 3. Préparation du payload d'Advertising BLE
    // L'UUID 16-bit pour OpenDroneID (ASTM F3411) est 0xFFFA.
    // Dans le payload Service Data, les deux premiers octets sont l'UUID (Little Endian).
    uint8_t svc_data[2 + sizeof(ODID_BasicID_encoded)];
    svc_data = 0xFA; 
    svc_data = 0xFF; 
    memcpy(&svc_data, &encoded_basic_id, sizeof(ODID_BasicID_encoded));

    struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
        BT_DATA(BT_DATA_SVC_DATA16, svc_data, sizeof(svc_data))
    };

    // 4. Lancement de l'émission (Advertising)
    err = bt_le_adv_start(BT_LE_ADV_NCONN, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        printk("Erreur lors du démarrage de l'advertising (err %d)\n", err);
        return;
    }
    printk("Émission du DET en cours...\n");
}

int main(void)
{
    int err;
    printk("Démarrage de l'émulateur Remote ID...\n");

    err = bt_enable(bt_ready);
    if (err) {
        printk("Echec de l'activation Bluetooth (err %d)\n", err);
    }

    return 0;
}