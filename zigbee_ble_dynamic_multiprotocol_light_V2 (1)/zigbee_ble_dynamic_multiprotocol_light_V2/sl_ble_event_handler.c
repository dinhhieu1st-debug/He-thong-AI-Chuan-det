/***************************************************************************//**
 * @file
 * @brief BLE related application related common code in the Zigbee BLE DMP sample apps
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#include PLATFORM_HEADER
#include "hal.h"
#include "sl_zigbee.h"
#include "app/framework/include/af.h"

#include "sl_bluetooth.h"
#include "sl_bluetooth_advertiser_config.h"
#include "sl_bluetooth_connection_config.h"
#ifdef SL_COMPONENT_CATALOG_PRESENT
#include "sl_component_catalog.h"
#endif
#ifdef SL_CATALOG_ZIGBEE_DISPLAY_PRESENT
#include "sl_dmp_ui.h"
#else
#include "sl_dmp_ui_stub.h"
#endif // SL_CATALOG_ZIGBEE_DISPLAY_PRESENT

#include "gatt_db.h"
#ifdef SL_CATALOG_ZIGBEE_DEBUG_PRINT_PRESENT
#include "sl_zigbee_debug_print.h"
#endif // SL_CATALOG_ZIGBEE_DEBUG_PRINT_PRESENT
#include "sl_ble_event_handler.h"
#include <string.h>

sl_zigbee_af_event_t               attr_write_event;
#define attrWriteEvent          (&attr_write_event)
static void attrWriteEventHandler(sl_zigbee_af_event_t * event);

sl_zigbee_af_event_t               xg26_rejoin_event;
#define xg26RejoinEvent          (&xg26_rejoin_event)
static void xg26RejoinEventHandler(sl_zigbee_af_event_t * event);

sl_zigbee_af_event_t               xg26_join_timeout_event;
#define xg26JoinTimeoutEvent     (&xg26_join_timeout_event)
static void xg26JoinTimeoutEventHandler(sl_zigbee_af_event_t * event);
static uint8_t xg26_rejoin_connection = 0xFF;
/*
 * true  : event rejoin duoc kich hoat tu lenh 0x0A ZB_JOIN demo, nen app se nhan lai code JOIN_STARTED sau khi bat dau steering.
 * false : event rejoin duoc kich hoat tu lenh 0x0E ZB_REJOIN rieng, nen app se nhan code REJOIN_JOIN_STARTED.
 */
static bool xg26_rejoin_report_as_join = false;
static bool xg26_join_in_progress = false;
static uint8_t xg26_join_retry_count = 0;
#define XG26_JOIN_TIMEOUT_MS 45000
#define XG26_JOIN_MAX_RETRIES 2
#define XG26_JOIN_RETRY_DELAY_MS 1500

/*
 * XG26 BLE provisioning command over existing DMP Light State characteristic.
 * Android writes 1 byte into gattdb_light_state:
 *   10 = ZB_JOIN   : demo mode - if joined, leave first, then start Network Steering again
 *   11 = ZB_LEAVE  : leave current Zigbee network
 *   12 = ZB_STATUS : print current Zigbee status
 *   13 = GET_INFO  : print device info
 *   14 = ZB_REJOIN  : leave current network, then start network steering
 *
 * This keeps the GATT database unchanged for the first test version.
 */
#define XG26_CMD_ZB_JOIN      10
#define XG26_CMD_ZB_LEAVE     11
#define XG26_CMD_ZB_STATUS    12
#define XG26_CMD_GET_INFO     13
#define XG26_CMD_ZB_SCAN      14
#define XG26_CMD_ZB_REJOIN    15

#define XG26_RESP_OK                 0
#define XG26_RESP_JOIN_STARTED       20
#define XG26_RESP_JOIN_START_FAILED  21
#define XG26_RESP_ALREADY_JOINED     22
#define XG26_RESP_LEAVE_STARTED      30
#define XG26_RESP_LEAVE_FAILED       31
#define XG26_RESP_STATUS_PRINTED     40
#define XG26_RESP_INFO_PRINTED       50
#define XG26_RESP_SCAN_RESULT        70
#define XG26_RESP_REJOIN_STARTED     60
#define XG26_RESP_REJOIN_LEAVE_FAILED 61
#define XG26_RESP_REJOIN_JOIN_STARTED 62
#define XG26_RESP_REJOIN_JOIN_FAILED  63
#define XG26_RESP_JOIN_IN_PROGRESS   23
#define XG26_RESP_UNKNOWN_COMMAND    99

/* API provided by the Zigbee Network Steering component. */
extern sl_status_t sl_zigbee_af_network_steering_start(void);
extern sl_status_t sl_zigbee_af_network_steering_stop(void);
extern uint8_t sli_zigbee_af_network_steering_options_mask;

static void xg26_ble_send_response_code(uint8_t connection, uint8_t code);
static void xg26_ble_send_text_frame(uint8_t connection, const char *text);
static void xg26_ble_send_scan_not_supported(uint8_t connection);
static void xg26_print_zigbee_status(void);
static void xg26_print_device_info(void);
static bool xg26_ble_handle_command(uint8_t connection, byte_array *writeValue);

void enableBleAdvertisements(void);
void BeaconAdvertisements(uint16_t devId);

void bleConnectionInfoTableInit(void);
uint8_t bleConnectionInfoTableFindUnused(void);
bool bleConnectionInfoTableIsEmpty(void);
void bleConnectionInfoTablePrintEntry(uint8_t index);
uint8_t bleConnectionInfoTableLookup(uint8_t connHandle);

#define SOURCE_ADDRESS_LEN 8
static uint8_t ble_lightState = DMP_UI_LIGHT_OFF;
static uint8_t ble_lastEvent = DMP_UI_DIRECTION_INVALID;
static uint8_t activeBleConnections = 0;

static sl_bt_gatt_client_config_flag_t ble_lightState_config = sl_bt_gatt_disable;
static sl_bt_gatt_client_config_flag_t ble_triggerSrc_config = sl_bt_gatt_disable;
static sl_bt_gatt_client_config_flag_t ble_bleSrc_config = sl_bt_gatt_disable;

static uint8_t SourceAddress[SOURCE_ADDRESS_LEN];

// Advertisement data
#define UINT16_TO_BYTES(n)        ((uint8_t) (n)), ((uint8_t)((n) >> 8))
#define UINT16_TO_BYTE0(n)        ((uint8_t) (n))
#define UINT16_TO_BYTE1(n)        ((uint8_t) ((n) >> 8))
// Ble TX test macros and functions
#define BLE_TX_TEST_DATA_SIZE   2
// We need to put the device name into a scan response packet,
// since it isn't included in the 'standard' beacons -
// I've included the flags, since certain apps seem to expect them
#define DEVNAME "xG26"
#define DEVNAME_LEN 8  // giu 8 de khong loi cac mang shortName co san
#define UUID_LEN 16 // 128-bit UUID
#define SOURCE_ADDRESS_LEN 8

#define OTA_SCAN_RESPONSE_DATA        0x04
#define OTA_ADVERTISING_DATA          0x02
#define PUBLIC_DEVICE_ADDRESS         0
#define STATIC_RANDOM_ADDRESS         1
#define LE_GAP_NON_RESOLVABLE         0x04

// BLE CHARACTERISTIC RELATED  ---
/** GATT Server Attribute User Read Configuration.
 *  Structure to register handler functions to user read events. */
typedef struct {
  uint16_t charId; /**< ID of the Characteristic. */
  void (*fctn)(uint8_t connection); /**< Handler function. */
} sli_zigbee_app_cfg_gatt_server_user_read_request_t;

/** GATT Server Attribute Value Write Configuration.
 *  Structure to register handler functions to characteristic write events. */
typedef struct {
  uint16_t charId; /**< ID of the Characteristic. */
  /**< Handler function. */
  void (*fctn)(uint8_t connection, byte_array * writeValue);
} sli_zigbee_app_cfg_gatt_server_user_write_request_t;

static const sli_zigbee_app_cfg_gatt_server_user_read_request_t appCfgGattServerUserReadRequest[] =
{
  { gattdb_light_state, zb_ble_dmp_read_light_state },
  { gattdb_trigger_source, zb_ble_dmp_read_trigger_source },
  { gattdb_source_address, zb_ble_dmp_read_source_address },
  { 0, NULL }
};

static const sli_zigbee_app_cfg_gatt_server_user_write_request_t appCfgGattServerUserWriteRequest[] =
{
  { gattdb_light_state, zb_ble_dmp_write_light_state },
  { 0, NULL }
};

size_t appCfgGattServerUserReadRequestSize = COUNTOF(appCfgGattServerUserReadRequest) - 1;
size_t appCfgGattServerUserWriteRequestSize = COUNTOF(appCfgGattServerUserWriteRequest) - 1;
// --- BLE CHARACTERISTIC RELATED

/* Advertising handles */
enum {
  HANDLE_DEMO = 0,
  HANDLE_IBEACON = 1,
  HANDLE_EDDYSTONE = 2,

  MAX_ADV_HANDLES = 3
};
uint8_t adv_handle[MAX_ADV_HANDLES];

struct {
  bool inUse;
  bool isMaster;
  uint8_t connectionHandle;
  uint8_t bondingHandle;
  uint8_t remoteAddress[6];
} bleConnectionTable[SL_BT_CONFIG_MAX_CONNECTIONS];

struct responseData_t {
  uint8_t flagsLen; /**< Length of the Flags field. */
  uint8_t flagsType; /**< Type of the Flags field. */
  uint8_t flags; /**< Flags field. */
  uint8_t shortNameLen; /**< Length of Shortened Local Name. */
  uint8_t shortNameType; /**< Shortened Local Name. */
  uint8_t shortName[DEVNAME_LEN]; /**< Shortened Local Name. */
  uint8_t uuidLength; /**< Length of UUID. */
  uint8_t uuidType; /**< Type of UUID. */
  uint8_t uuid[UUID_LEN]; /**< 128-bit UUID. */
};

static struct responseData_t responseData = { 2, /* length (incl type) */
                                              0x01, /* type */
                                              0x04 | 0x02, /* Flags: LE General Discoverable Mode, BR/EDR is disabled. */
                                              DEVNAME_LEN + 1, // length of local name (incl type)
                                              0x08, // shortened local name
                                              { 'D', 'M', '0', '0', ':', '0', '0' },
                                              UUID_LEN + 1, // length of UUID data (incl type)
                                              0x06, // incomplete list of service UUID's
                                              // custom service UUID for silabs lamp in little-endian format
                                              { 0xc9, 0x1b, 0x80, 0x3d, 0x61, 0x50, 0x0c, 0x97, 0x8d, 0x45, 0x19,
                                                0x7d, 0x96, 0x5b, 0xe5, 0xba } };

// iBeacon structure and data
static struct {
  uint8_t flagsLen; /* Length of the Flags field. */
  uint8_t flagsType; /* Type of the Flags field. */
  uint8_t flags; /* Flags field. */
  uint8_t mandataLen; /* Length of the Manufacturer Data field. */
  uint8_t mandataType; /* Type of the Manufacturer Data field. */
  uint8_t compId[2]; /* Company ID field. */
  uint8_t beacType[2]; /* Beacon Type field. */
  uint8_t uuid[16]; /* 128-bit Universally Unique Identifier (UUID). The UUID is an identifier for the company using the beacon*/
  uint8_t majNum[2]; /* Beacon major number. Used to group related beacons. */
  uint8_t minNum[2]; /* Beacon minor number. Used to specify individual beacons within a group.*/
  uint8_t txPower; /* The Beacon's measured RSSI at 1 meter distance in dBm. See the iBeacon specification for measurement guidelines. */
} iBeaconData = {
/* Flag bits - See Bluetooth 4.0 Core Specification , Volume 3, Appendix C, 18.1 for more details on flags. */
  2, /* length  */
  0x01, /* type */
  0x04 | 0x02, /* Flags: LE General Discoverable Mode, BR/EDR is disabled. */

/* Manufacturer specific data */
  26, /* length of field*/
  0xFF, /* type of field */

/* The first two data octets shall contain a company identifier code from
 * the Assigned Numbers - Company Identifiers document */
  { UINT16_TO_BYTES(0x004C) },

/* Beacon type */
/* 0x0215 is iBeacon */
  { UINT16_TO_BYTE1(0x0215), UINT16_TO_BYTE0(0x0215) },

/* 128 bit / 16 byte UUID - generated specially for the DMP Demo */
  { 0x00, 0x47, 0xe7, 0x0a, 0x5d, 0xc1, 0x47, 0x25, 0x87, 0x99, 0x83, 0x05, 0x44,
    0xae, 0x04, 0xf6 },

/* Beacon major number - not used for this application */
  { UINT16_TO_BYTE1(256), UINT16_TO_BYTE0(256) },

/* Beacon minor number  - not used for this application*/
  { UINT16_TO_BYTE1(0), UINT16_TO_BYTE0(0) },

/* The Beacon's measured RSSI at 1 meter distance in dBm */
/* 0xC3 is -61dBm */
// TBD: check?
  0xC3
};

static struct {
  uint8_t flagsLen; /**< Length of the Flags field. */
  uint8_t flagsType; /**< Type of the Flags field. */
  uint8_t flags; /**< Flags field. */
  uint8_t serLen; /**< Length of Complete list of 16-bit Service UUIDs. */
  uint8_t serType; /**< Complete list of 16-bit Service UUIDs. */
  uint8_t serviceList[2]; /**< Complete list of 16-bit Service UUIDs. */
  uint8_t serDataLength; /**< Length of Service Data. */
  uint8_t serDataType; /**< Type of Service Data. */
  uint8_t uuid[2]; /**< 16-bit Eddystone UUID. */
  uint8_t frameType; /**< Frame type. */
  uint8_t txPower; /**< The Beacon's measured RSSI at 0 meter distance in dBm. */
  uint8_t urlPrefix; /**< URL prefix type. */
  uint8_t url[10]; /**< URL. */
} eddystone_data = {
/* Flag bits - See Bluetooth 4.0 Core Specification , Volume 3, Appendix C, 18.1 for more details on flags. */
  2, /* length  */
  0x01, /* type */
  0x04 | 0x02, /* Flags: LE General Discoverable Mode, BR/EDR is disabled. */
/* Service field length */
  0x03,
/* Service field type */
  0x03,
/* 16-bit Eddystone UUID */
  { UINT16_TO_BYTES(0xFEAA) },
/* Eddystone-TLM Frame length */
  0x10,
/* Service Data data type value */
  0x16,
/* 16-bit Eddystone UUID */
  { UINT16_TO_BYTES(0xFEAA) },
/* Eddystone-URL Frame type */
  0x10,
/* Tx power */
  0x00,
/* URL prefix - standard */
  0x00,
/* URL */
  { 's', 'i', 'l', 'a', 'b', 's', '.', 'c', 'o', 'm' }
};

// to convert hex number to its ascii character
uint8_t ascii_lut[] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

void zb_ble_dmp_print_ble_address(uint8_t *address)
{
  sl_zigbee_app_debug_print("\nBLE address: [%02X %02X %02X %02X %02X %02X]\n",
                            address[5], address[4], address[3],
                            address[2], address[1], address[0]);
}

void bleConnectionInfoTableInit(void)
{
  uint8_t i;
  for (i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS; i++) {
    bleConnectionTable[i].inUse = false;
  }
}

uint8_t bleConnectionInfoTableFindUnused(void)
{
  uint8_t i;
  for (i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS; i++) {
    if (!bleConnectionTable[i].inUse) {
      return i;
    }
  }
  return 0xFF;
}

bool bleConnectionInfoTableIsEmpty(void)
{
  uint8_t i;
  for (i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS; i++) {
    if (bleConnectionTable[i].inUse) {
      return false;
    }
  }
  return true;
}

uint8_t bleConnectionInfoTableLookup(uint8_t connHandle)
{
  uint8_t i;
  for (i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS; i++) {
    if (bleConnectionTable[i].inUse
        && bleConnectionTable[i].connectionHandle == connHandle) {
      return i;
    }
  }
  return 0xFF;
}

void bleConnectionInfoTablePrintEntry(uint8_t index)
{
  assert(index < SL_BT_CONFIG_MAX_CONNECTIONS
         && bleConnectionTable[index].inUse);
  sl_zigbee_app_debug_println("**** Connection Info index[%d]****", index);
  sl_zigbee_app_debug_println("connection handle 0x%02X",
                              bleConnectionTable[index].connectionHandle);
  sl_zigbee_app_debug_println("bonding handle = 0x%02X",
                              bleConnectionTable[index].bondingHandle);
  sl_zigbee_app_debug_println("local node is %s",
                              (bleConnectionTable[index].isMaster) ? "master" : "slave");
  sl_zigbee_app_debug_print("remote address: ");
  zb_ble_dmp_print_ble_address(bleConnectionTable[index].remoteAddress);
  sl_zigbee_app_debug_println("");
  sl_zigbee_app_debug_println("*************************");
}

static void xg26_ble_send_response_code(uint8_t connection, uint8_t code)
{
  ble_lastEvent = code;

  sl_zigbee_app_debug_println("[XG26] BLE response code = %d", code);

  /*
   * The response is sent through the existing trigger_source characteristic.
   * On the phone side, enable notification or indication for trigger_source first.
   */
  if ((ble_triggerSrc_config & sl_bt_gatt_indication) != 0) {
    sl_status_t status = sl_bt_gatt_server_send_indication(connection,
                                                           gattdb_trigger_source,
                                                           sizeof(code),
                                                           &code);
    if (status != SL_STATUS_OK) {
      sl_zigbee_app_debug_println("[XG26] send response indication failed: 0x%04X", (unsigned int)status);
    }
  } else if ((ble_triggerSrc_config & sl_bt_gatt_notification) != 0) {
    sl_status_t status = sl_bt_gatt_server_send_notification(connection,
                                                             gattdb_trigger_source,
                                                             sizeof(code),
                                                             &code);
    if (status != SL_STATUS_OK) {
      sl_zigbee_app_debug_println("[XG26] send response notification failed: 0x%04X", (unsigned int)status);
    }
  } else {
    sl_zigbee_app_debug_println("[XG26] trigger_source notify/indicate is not enabled by phone");
  }
}


static void xg26_ble_send_text_frame(uint8_t connection, const char *text)
{
  if (text == NULL) {
    return;
  }

  uint16_t len = (uint16_t)strlen(text);
  sl_status_t status = SL_STATUS_FAIL;

  sl_zigbee_app_debug_println("[XG26] BLE text frame: %s", text);

  if ((ble_triggerSrc_config & sl_bt_gatt_notification) != 0) {
    status = sl_bt_gatt_server_send_notification(connection,
                                                 gattdb_trigger_source,
                                                 len,
                                                 (const uint8_t *)text);
  } else if ((ble_triggerSrc_config & sl_bt_gatt_indication) != 0) {
    status = sl_bt_gatt_server_send_indication(connection,
                                               gattdb_trigger_source,
                                               len,
                                               (const uint8_t *)text);
  }

  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("[XG26] send text frame failed: 0x%04X", (unsigned int)status);
  }
}

static void xg26_ble_send_scan_not_supported(uint8_t connection)
{
  xg26_ble_send_text_frame(connection, "SCAN_BEGIN");
  xg26_ble_send_text_frame(connection, "SCAN_CHUNK:{\"cmd\":\"ZB_SCAN\",\"status\":\"NOT_SUPPORTED\",\"message\":\"Firmware hien tai chua ho tro quet mang Zigbee\"}");
  xg26_ble_send_text_frame(connection, "SCAN_END");
}
static void xg26_print_zigbee_status(void)
{
  sl_zigbee_network_status_t state = sl_zigbee_network_state();
  bool stack_up = sl_zigbee_stack_is_up();
  uint8_t channel = sl_zigbee_get_radio_channel();
  sl_802154_pan_id_t pan_id = sl_zigbee_get_pan_id();
  sl_802154_short_addr_t node_id = sl_zigbee_get_node_id();

  sl_zigbee_app_debug_println("[XG26] Zigbee status:");
  sl_zigbee_app_debug_println("[XG26]   stack_up      = %d", stack_up ? 1 : 0);
  sl_zigbee_app_debug_println("[XG26]   network_state = 0x%02X", (unsigned int)state);
  sl_zigbee_app_debug_println("[XG26]   channel       = %d", channel);
  sl_zigbee_app_debug_println("[XG26]   panId         = 0x%04X", pan_id);
  sl_zigbee_app_debug_println("[XG26]   nodeId        = 0x%04X", node_id);
}

static void xg26_print_device_info(void)
{
  uint8_t *eui64 = sl_zigbee_get_eui64();

  sl_zigbee_app_debug_println("[XG26] Device info:");
  sl_zigbee_app_debug_print("[XG26]   EUI64 = 0x");
  for (int i = 7; i >= 0; i--) {
    sl_zigbee_app_debug_print("%02X", eui64[i]);
  }
  sl_zigbee_app_debug_println("");

  xg26_print_zigbee_status();
}

static bool xg26_ble_handle_command(uint8_t connection, byte_array *writeValue)
{
  if (writeValue == NULL || writeValue->len == 0) {
    return false;
  }

  uint8_t cmd = writeValue->data[0];
  sl_status_t status;

  switch (cmd) {
    case XG26_CMD_ZB_JOIN:
      sl_zigbee_app_debug_println("[XG26] BLE command: ZB_JOIN demo mode - leave first, then join");

      if (xg26_join_in_progress) {
        sl_zigbee_app_debug_println("[XG26] Network steering is already running. Force restart from phone request...");
        sl_zigbee_af_event_set_inactive(xg26JoinTimeoutEvent);
        sl_zigbee_af_event_set_inactive(xg26RejoinEvent);
        status = sl_zigbee_af_network_steering_stop();
        sl_zigbee_app_debug_println("[XG26] sl_zigbee_af_network_steering_stop status = 0x%04X", (unsigned int)status);
        xg26_join_in_progress = false;
        xg26_join_retry_count = 0;
        xg26_rejoin_connection = connection;
        xg26_rejoin_report_as_join = true;
        sli_zigbee_af_network_steering_options_mask = 0;
        sl_zigbee_app_debug_println("[XG26] Force restarting network steering with option 0 now...");
        status = sl_zigbee_af_network_steering_start();
        sl_zigbee_app_debug_println("[XG26] sl_zigbee_af_network_steering_start status = 0x%04X", (unsigned int)status);
        if (status == SL_STATUS_OK) {
          xg26_join_in_progress = true;
          sl_zigbee_af_event_set_delay_ms(xg26JoinTimeoutEvent, XG26_JOIN_TIMEOUT_MS);
          xg26_ble_send_response_code(connection, XG26_RESP_JOIN_STARTED);
        } else {
          xg26_ble_send_response_code(connection, XG26_RESP_JOIN_START_FAILED);
        }
        return true;
      }

      /*
       * DEMO MODE:
       * App Android gui 0x0A = Tham gia Zigbee.
       * Yeu cau demo: moi lan bam nut Tham gia Zigbee thi xG26 phai goi
       * sl_zigbee_leave_network() truoc, sau do moi chay Network Steering.
       *
       * Ly do:
       * - Neu thiet bi dang o trong mang cu: can roi mang cu truoc.
       * - Neu thiet bi chua o trong mang nao: leave co the tra ve khac OK,
       *   vi du 0x02, nhung van khong coi la loi nghiem trong trong demo.
       *   Ta van cho mot khoang tre ngan roi start Network Steering.
       */
      xg26_rejoin_connection = connection;
      xg26_rejoin_report_as_join = true;
      xg26_join_retry_count = 0;

      xg26_print_zigbee_status();

      /*
       * Neu thiet bi chua o trong mang Zigbee thi khong goi leave nua.
       * Truoc day leave tra 0x0002 roi moi hen gio steering, nen demo phai go
       * them lenh CLI "network leave" moi de test. Bat dau steering truc tiep
       * de nut Tham gia Zigbee tren app tu lam tron quy trinh.
       */
      if (!sl_zigbee_stack_is_up()) {
        sl_zigbee_app_debug_println("[XG26] Device is not joined. Running plugin network-steering start 0 equivalent...");
        sli_zigbee_af_network_steering_options_mask = 0;
        sl_zigbee_app_debug_println("[XG26] Starting network steering with option 0 now...");
        status = sl_zigbee_af_network_steering_start();
        sl_zigbee_app_debug_println("[XG26] sl_zigbee_af_network_steering_start status = 0x%04X", (unsigned int)status);
        if (status == SL_STATUS_OK) {
          xg26_join_in_progress = true;
          sl_zigbee_af_event_set_delay_ms(xg26JoinTimeoutEvent, XG26_JOIN_TIMEOUT_MS);
          xg26_ble_send_response_code(connection, XG26_RESP_JOIN_STARTED);
        } else {
          xg26_ble_send_response_code(connection, XG26_RESP_JOIN_START_FAILED);
        }
        return true;
      }

      status = sl_zigbee_leave_network(SL_ZIGBEE_LEAVE_NWK_WITH_NO_OPTION);
      sl_zigbee_app_debug_println("[XG26] sl_zigbee_leave_network status = 0x%04X", (unsigned int)status);

      if (status == SL_STATUS_OK) {
        xg26_join_in_progress = true;
        sl_zigbee_af_event_set_delay_ms(xg26JoinTimeoutEvent, XG26_JOIN_TIMEOUT_MS);
        xg26_ble_send_response_code(connection, XG26_RESP_REJOIN_STARTED);
        sl_zigbee_af_event_set_delay_ms(xg26RejoinEvent, 3000);
      } else {
        sl_zigbee_app_debug_println("[XG26] leave returned non-OK. Starting network steering directly...");
        status = sl_zigbee_af_network_steering_start();
        sl_zigbee_app_debug_println("[XG26] sl_zigbee_af_network_steering_start status = 0x%04X", (unsigned int)status);
        xg26_rejoin_connection = 0xFF;
        xg26_rejoin_report_as_join = false;
        if (status == SL_STATUS_OK) {
          xg26_join_in_progress = true;
          sl_zigbee_af_event_set_delay_ms(xg26JoinTimeoutEvent, XG26_JOIN_TIMEOUT_MS);
          xg26_ble_send_response_code(connection, XG26_RESP_JOIN_STARTED);
        } else {
          xg26_ble_send_response_code(connection, XG26_RESP_JOIN_START_FAILED);
        }
      }
      return true;

    case XG26_CMD_ZB_LEAVE:
      sl_zigbee_app_debug_println("[XG26] BLE command: ZB_LEAVE");
      xg26_join_in_progress = false;
      xg26_join_retry_count = 0;
      sl_zigbee_af_event_set_inactive(xg26JoinTimeoutEvent);
      sl_zigbee_af_event_set_inactive(xg26RejoinEvent);
      status = sl_zigbee_leave_network(SL_ZIGBEE_LEAVE_NWK_WITH_NO_OPTION);
      sl_zigbee_app_debug_println("[XG26] sl_zigbee_leave_network status = 0x%04X", (unsigned int)status);

      if (status == SL_STATUS_OK) {
        xg26_ble_send_response_code(connection, XG26_RESP_LEAVE_STARTED);
      } else {
        xg26_ble_send_response_code(connection, XG26_RESP_LEAVE_FAILED);
      }
      return true;

    case XG26_CMD_ZB_STATUS:
      sl_zigbee_app_debug_println("[XG26] BLE command: ZB_STATUS");
      xg26_print_zigbee_status();
      xg26_ble_send_response_code(connection, XG26_RESP_STATUS_PRINTED);
      return true;

    case XG26_CMD_GET_INFO:
      sl_zigbee_app_debug_println("[XG26] BLE command: GET_INFO");
      xg26_print_device_info();
      xg26_ble_send_response_code(connection, XG26_RESP_INFO_PRINTED);
      return true;

    case XG26_CMD_ZB_SCAN:
      sl_zigbee_app_debug_println("[XG26] BLE command: ZB_SCAN");
      sl_zigbee_app_debug_println("[XG26] Start Zigbee network scan...");
      sl_zigbee_app_debug_println("[XG26] ZB_SCAN not supported in current firmware build");
      xg26_ble_send_scan_not_supported(connection);
      xg26_ble_send_response_code(connection, XG26_RESP_SCAN_RESULT);
      return true;

    case XG26_CMD_ZB_REJOIN:
      sl_zigbee_app_debug_println("[XG26] BLE command: ZB_REJOIN");
      xg26_rejoin_connection = connection;
      xg26_rejoin_report_as_join = false;

      if (sl_zigbee_stack_is_up()) {
        sl_zigbee_app_debug_println("[XG26] Device is joined. Leaving first, then rejoin after delay...");
        status = sl_zigbee_leave_network(SL_ZIGBEE_LEAVE_NWK_WITH_NO_OPTION);
        sl_zigbee_app_debug_println("[XG26] sl_zigbee_leave_network status = 0x%04X", (unsigned int)status);

        if (status == SL_STATUS_OK) {
          xg26_join_in_progress = true;
          sl_zigbee_af_event_set_delay_ms(xg26JoinTimeoutEvent, XG26_JOIN_TIMEOUT_MS);
          xg26_ble_send_response_code(connection, XG26_RESP_REJOIN_STARTED);
          sl_zigbee_af_event_set_delay_ms(xg26RejoinEvent, 3000);
        } else {
          xg26_ble_send_response_code(connection, XG26_RESP_REJOIN_LEAVE_FAILED);
        }
        return true;
      }

      sl_zigbee_app_debug_println("[XG26] Device is not joined. Starting network steering directly...");
      status = sl_zigbee_af_network_steering_start();
      sl_zigbee_app_debug_println("[XG26] sl_zigbee_af_network_steering_start status = 0x%04X", (unsigned int)status);

      xg26_rejoin_connection = 0xFF;
      xg26_rejoin_report_as_join = false;

      if (status == SL_STATUS_OK) {
        xg26_ble_send_response_code(connection, XG26_RESP_REJOIN_JOIN_STARTED);
      } else {
        xg26_ble_send_response_code(connection, XG26_RESP_REJOIN_JOIN_FAILED);
      }
      return true;

    default:
      /* 0 and 1 are kept for the original light on/off demo behavior. */
      if (cmd != 0 && cmd != 1) {
        sl_zigbee_app_debug_println("[XG26] Unknown BLE command: %d", cmd);
        xg26_ble_send_response_code(connection, XG26_RESP_UNKNOWN_COMMAND);
        return true;
      }
      return false;
  }
}

/* Characteristic read / write / notify handler functions */
void zb_ble_dmp_read_light_state(uint8_t connection)
{
  uint16_t sent_data_len;
  sl_zigbee_app_debug_println("Light state = %d\r\n", ble_lightState);
  /* Send response to read request */
  sl_status_t status =  sl_bt_gatt_server_send_user_read_response(connection,
                                                                  gattdb_light_state,
                                                                  SL_STATUS_OK,
                                                                  sizeof(ble_lightState),
                                                                  &ble_lightState,
                                                                  &sent_data_len);

  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Failed to zb_ble_dmp_read_light_state");
  }
}

void zb_ble_dmp_read_trigger_source(uint8_t connection)
{
  uint16_t sent_data_len;
  sl_zigbee_app_debug_println("Last event = %d\r\n", ble_lastEvent);

  /* Send response to read request */
  sl_status_t status =  sl_bt_gatt_server_send_user_read_response(connection,
                                                                  gattdb_trigger_source,
                                                                  SL_STATUS_OK,
                                                                  sizeof(ble_lastEvent),
                                                                  &ble_lastEvent,
                                                                  &sent_data_len);
  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Failed to zb_ble_dmp_read_trigger_source");
  }
}

void zb_ble_dmp_read_source_address(uint8_t connection)
{
  uint16_t sent_data_len;
  sl_zigbee_app_debug_println("zb_ble_dmp_read_source_address");

  /* Send response to read request */
  sl_status_t status =  sl_bt_gatt_server_send_user_read_response(connection,
                                                                  gattdb_source_address,
                                                                  SL_STATUS_OK,
                                                                  sizeof(SourceAddress),
                                                                  SourceAddress,
                                                                  &sent_data_len);

  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Failed to zb_ble_dmp_read_source_address");
  }
}

void zb_ble_dmp_write_light_state(uint8_t connection, byte_array *writeValue)
{
  if (writeValue == NULL || writeValue->len == 0) {
    sl_zigbee_app_debug_println("[XG26] Empty BLE write on light_state");

    sl_status_t status = sl_bt_gatt_server_send_user_write_response(connection,
                                                                    gattdb_light_state,
                                                                    SL_STATUS_INVALID_PARAMETER);
    if (status != SL_STATUS_OK) {
      sl_zigbee_app_debug_println("[XG26] Failed to respond empty BLE write: 0x%04X", (unsigned int)status);
    }
    return;
  }

  sl_zigbee_app_debug_println("Light state / command write; %d\r\n", writeValue->data[0]);

  /*
   * First, check whether the written byte is an XG26 provisioning command.
   * If yes, do not run the old light on/off behavior.
   */
  if (xg26_ble_handle_command(connection, writeValue)) {
    sl_status_t status = sl_bt_gatt_server_send_user_write_response(connection,
                                                                    gattdb_light_state,
                                                                    SL_STATUS_OK);
    if (status != SL_STATUS_OK) {
      sl_zigbee_app_debug_println("[XG26] Failed to respond BLE command write: 0x%04X", (unsigned int)status);
    }
    return;
  }

  sl_dmp_ui_set_light_direction(DMP_UI_DIRECTION_BLUETOOTH);
  ble_lightState = writeValue->data[0];

  sl_zigbee_af_event_set_active(attrWriteEvent);
  sl_zigbee_wakeup_common_task();

  sl_status_t status = sl_bt_gatt_server_send_user_write_response(connection,
                                                                  gattdb_light_state,
                                                                  SL_STATUS_OK);

  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Failed to zb_ble_dmp_write_light_state");
    return;
  }

  uint8_t index = bleConnectionInfoTableLookup(connection);

  if (index != 0xFF) {
    (void) memset(SourceAddress, 0, sizeof(SourceAddress));
    for (int i = 0; i < SOURCE_ADDRESS_LEN - 2; i++) {
      SourceAddress[2 + i] =
        bleConnectionTable[index].remoteAddress[5 - i];
    }
  }
}

void zb_ble_dmp_notify_light(uint8_t lightState)
{
  ble_lightState = lightState;
  sl_status_t status;

  if (ble_lightState_config == sl_bt_gatt_indication) {
    sl_zigbee_app_debug_println("zb_ble_dmp_notify_light: Light state = %d\r\n", lightState);
    /* Send notification/indication data */
    for (int i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS; i++) {
      if (bleConnectionTable[i].inUse
          && bleConnectionTable[i].connectionHandle) {
        status = sl_bt_gatt_server_send_indication(bleConnectionTable[i].connectionHandle,
                                                   gattdb_light_state,
                                                   sizeof(lightState),
                                                   &lightState);

        if (status != SL_STATUS_OK) {
          sl_zigbee_app_debug_println("Failed to zb_ble_dmp_notify_light error : 0x%02X", status);
          return;
        }
      }
    }
  }
}

void zb_ble_dmp_notify_trigger_source(uint8_t connection, uint8_t triggerSource)
{
  sl_status_t status;

  if ((ble_triggerSrc_config & sl_bt_gatt_indication) != 0) {
    sl_zigbee_app_debug_println("zb_ble_dmp_notify_trigger_source :Last event = %d\r\n",
                                triggerSource);
    status = sl_bt_gatt_server_send_indication(connection,
                                               gattdb_trigger_source,
                                               sizeof(triggerSource),
                                               &triggerSource);

    if (status != SL_STATUS_OK) {
      sl_zigbee_app_debug_println("Failed to zb_ble_dmp_notify_trigger_source indication");
      return;
    }
  } else if ((ble_triggerSrc_config & sl_bt_gatt_notification) != 0) {
    sl_zigbee_app_debug_println("zb_ble_dmp_notify_trigger_source :Last event = %d\r\n",
                                triggerSource);
    status = sl_bt_gatt_server_send_notification(connection,
                                                 gattdb_trigger_source,
                                                 sizeof(triggerSource),
                                                 &triggerSource);

    if (status != SL_STATUS_OK) {
      sl_zigbee_app_debug_println("Failed to zb_ble_dmp_notify_trigger_source notification");
      return;
    }
  }
}
void zb_ble_dmp_notify_source_address(uint8_t connection)
{
  sl_status_t status;

  if ((ble_bleSrc_config & sl_bt_gatt_indication) != 0) {
    status = sl_bt_gatt_server_send_indication(connection,
                                               gattdb_source_address,
                                               sizeof(SourceAddress),
                                               SourceAddress);
    if (status != SL_STATUS_OK) {
      sl_zigbee_app_debug_println("Failed to zb_ble_dmp_notify_source_address indication");
      return;
    }
  } else if ((ble_bleSrc_config & sl_bt_gatt_notification) != 0) {
    status = sl_bt_gatt_server_send_notification(connection,
                                                 gattdb_source_address,
                                                 sizeof(SourceAddress),
                                                 SourceAddress);
    if (status != SL_STATUS_OK) {
      sl_zigbee_app_debug_println("Failed to zb_ble_dmp_notify_source_address notification");
      return;
    }
  } else {
    sl_zigbee_app_debug_println("[XG26] source_address notify/indicate is not enabled by phone");
  }
}

void zb_ble_dmp_set_source_address(sl_802154_long_addr_t set_address)
{
  for (uint8_t i = 0; i < 8; i++) {
    SourceAddress[i] = set_address[(8 - 1) - i];
  }
}

void BeaconAdvertisements(uint16_t devId)
{
  static uint8_t *advData;
  static uint8_t advDataLen;
  sl_status_t status;

  iBeaconData.minNum[0] = UINT16_TO_BYTE1(devId);
  iBeaconData.minNum[1] = UINT16_TO_BYTE0(devId);

  advData = (uint8_t*) &iBeaconData;
  advDataLen = sizeof(iBeaconData);
  /* Set custom advertising data */
  status = sl_bt_legacy_advertiser_set_data(adv_handle[HANDLE_IBEACON], 0, advDataLen, advData);
  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Error sl_bt_legacy_advertiser_set_data code: 0x%0x", status);
    return;
  }

  status = sl_bt_advertiser_set_timing(adv_handle[HANDLE_IBEACON],   // handle
                                       (100 / 0.625), //100ms min adv interval in terms of 0.625ms
                                       (100 / 0.625), //100ms max adv interval in terms of 0.625ms
                                       0,   // duration : continue advertisement until stopped
                                       0);   // max_events :continue advertisement until stopped
  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Error iBeacon sl_bt_advertiser_set_timing code: 0x%0x", status);
    return;
  }

  status = sl_bt_advertiser_configure(adv_handle[HANDLE_IBEACON], LE_GAP_NON_RESOLVABLE);
  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Error iBeacon sl_bt_advertiser_configure code: 0x%0x", status);
    return;
  }

  status = sl_bt_legacy_advertiser_start(adv_handle[HANDLE_IBEACON],
                                         sl_bt_advertiser_non_connectable);
  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Error iBeacon sl_bt_legacy_advertiser_start code: 0x%0x", status);
    return;
  }

  advData = (uint8_t*) &eddystone_data;
  advDataLen = sizeof(eddystone_data);
  /* Set custom advertising data */
  status = sl_bt_legacy_advertiser_set_data(adv_handle[HANDLE_EDDYSTONE], 0, advDataLen, advData);
  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Error eddystone sl_bt_legacy_advertiser_set_data code: 0x%0x", status);
    return;
  }

  status = sl_bt_advertiser_set_timing(adv_handle[HANDLE_EDDYSTONE],   // handle
                                       (100 / 0.625), //100ms min adv interval in terms of 0.625ms
                                       (100 / 0.625), //100ms max adv interval in terms of 0.625ms
                                       0,   // duration : continue advertisement until stopped
                                       0);   // max_events :continue advertisement until stopped
  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Error eddystone sl_bt_advertiser_set_timing code: 0x%0x", status);
    return;
  }

  status = sl_bt_advertiser_configure(adv_handle[HANDLE_EDDYSTONE], LE_GAP_NON_RESOLVABLE);
  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Error eddystone sl_bt_advertiser_configure code: 0x%0x", status);
    return;
  }

  status = sl_bt_legacy_advertiser_start(adv_handle[HANDLE_EDDYSTONE],
                                         sl_bt_advertiser_non_connectable);
  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Error eddystone sl_bt_legacy_advertiser_start code: 0x%0x", status);
    return;
  }
}

void enableBleAdvertisements(void)
{
  sl_status_t status;

  /* Create the device Id and name based on the 16-bit truncated bluetooth address
     Copy to the local GATT database - this will be used by the BLE stack
     to put the local device name into the advertisements, but only if we are
     using default advertisements */
  uint8_t type;
  bd_addr ble_address;
  static char devName[DEVNAME_LEN];

  status = sl_bt_system_get_identity_address(&ble_address, &type);
  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Unable to get BLE address. Errorcode: 0x%02X", status);
    return;
  }

  /*
  * devId van can giu lai vi phia duoi ham con goi:
  * BeaconAdvertisements(devId);
  */
  uint16_t devId = ((uint16_t)ble_address.addr[1] << 8) + (uint16_t)ble_address.addr[0];

  /*
  * Dat ten BLE hien tren dien thoai la xG26.
  * DEVNAME_LEN giu bang 8 de khong anh huong cac cau truc co san cua project mau.
  */
  memset(devName, 0, sizeof(devName));
  strncpy(devName, DEVNAME, sizeof(devName) - 1);
  devName[sizeof(devName) - 1] = '\0';

  sl_zigbee_app_debug_println("devName = %s", devName);

  status = sl_bt_gatt_server_write_attribute_value(gattdb_device_name,
                                                  0,
                                                  strlen(devName),
                                                  (uint8_t *)devName);

  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Unable to set BLE device name. Errorcode: 0x%02X", status);
    return;
  }

  

  /* Copy the shortened device name to the response data, overwriting
     the default device name which is set at compile time */
  memcpy(((uint8_t*) &responseData) + 5, devName, 8);

  /* Set the advertising data and scan response data*/
  /* Note that the Wireless Gecko mobile app filters by a specific UUID and
     if the advertising data is not set, the device will not be found on the app*/
  status = sl_bt_legacy_advertiser_set_data(adv_handle[HANDLE_DEMO],
                                            0,      //advertising packets
                                            sizeof(responseData),
                                            (uint8_t*) &responseData);

  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Unable to set adv data sl_bt_legacy_advertiser_set_data. Errorcode: 0x%02X", status);
    return;
  }

  status = sl_bt_legacy_advertiser_set_data(adv_handle[HANDLE_DEMO],
                                            1,      //scan response packets
                                            sizeof(responseData),
                                            (uint8_t*) &responseData);

  if (status != SL_STATUS_OK) {
    sl_zigbee_app_debug_println("Unable to set scan response data sl_bt_legacy_advertiser_set_data. Errorcode: 0x%02X", status);
    return;
  }

  status = sl_bt_advertiser_set_timing(adv_handle[HANDLE_DEMO],
                                       (100 / 0.625), //100ms min adv interval in terms of 0.625ms
                                       (100 / 0.625), //100ms max adv interval in terms of 0.625ms
                                       0,   // duration : continue advertisement until stopped
                                       0);   // max_events :continue advertisement until stopped
  if (status != SL_STATUS_OK) {
    return;
  }
  status = sl_bt_advertiser_set_report_scan_request(adv_handle[HANDLE_DEMO], 1);   //scan request reported as events
  if (status != SL_STATUS_OK) {
    return;
  }
  /* Start advertising in user mode and enable connections*/
  status = sl_bt_legacy_advertiser_start(adv_handle[HANDLE_DEMO],
                                         sl_bt_advertiser_connectable_scannable);
  if ( status ) {
    sl_zigbee_app_debug_println("sl_bt_legacy_advertiser_start ERROR : status = 0x%0X", status);
  } else {
    sl_zigbee_app_debug_println("BLE custom advertisements enabled");
  }
  if (SL_BT_CONFIG_USER_ADVERTISERS >= 3) {
    BeaconAdvertisements(devId);
  }
}

/** @brief
 *
 * This function is called from the BLE stack to notify the application of a
 * stack event.
 */
#ifdef SL_CATALOG_MATTER_BLE_DMP_TEST_PRESENT
void zigbee_bt_on_event(sl_bt_msg_t* evt)
#else
void sl_bt_on_event(sl_bt_msg_t* evt)
#endif
{
  switch (SL_BT_MSG_ID(evt->header)) {
    /* This event indicates that a remote GATT client is attempting to read a value of an
     *  attribute from the local GATT database, where the attribute was defined in the GATT
     *  XML firmware configuration file to have type="user". */

    case sl_bt_evt_gatt_server_user_read_request_id:
      for (uint32_t i = 0; i < appCfgGattServerUserReadRequestSize; i++) {
        if ((appCfgGattServerUserReadRequest[i].charId
             == evt->data.evt_gatt_server_user_read_request.characteristic)
            && (appCfgGattServerUserReadRequest[i].fctn)) {
          appCfgGattServerUserReadRequest[i].fctn(
            evt->data.evt_gatt_server_user_read_request.connection);
        }
      }
      break;

    /* This event indicates that a remote GATT client is attempting to write a value of an
     * attribute in to the local GATT database, where the attribute was defined in the GATT
     * XML firmware configuration file to have type="user".  */

    case sl_bt_evt_gatt_server_user_write_request_id:
      for (uint32_t i = 0; i < appCfgGattServerUserWriteRequestSize; i++) {
        if ((appCfgGattServerUserWriteRequest[i].charId
             == evt->data.evt_gatt_server_user_write_request.characteristic)
            && (appCfgGattServerUserWriteRequest[i].fctn)) {
          appCfgGattServerUserWriteRequest[i].fctn(
            evt->data.evt_gatt_server_user_write_request.connection,
            &(evt->data.evt_gatt_server_user_write_request.value));
        }
      }
      break;

    case sl_bt_evt_system_boot_id: {
      bd_addr ble_address;
      uint8_t type;
      sl_status_t status = sl_bt_system_hello();
      sl_zigbee_app_debug_println("BLE hello: %s",
                                  (status == SL_STATUS_OK) ? "success" : "error");

      status = sl_bt_system_get_identity_address(&ble_address, &type);
      zb_ble_dmp_print_ble_address(ble_address.addr);

      #define SCAN_WINDOW 5
      #define SCAN_INTERVAL 10

      status = sl_bt_scanner_set_parameters(sl_bt_scanner_scan_mode_active,
                                            (uint16_t)SCAN_INTERVAL,
                                            (uint16_t)SCAN_WINDOW);

      status = sl_bt_advertiser_create_set(&adv_handle[HANDLE_DEMO]);
      if (status) {
        sl_zigbee_app_debug_println("sl_bt_advertiser_create_set status 0x%02x", status);
      }

      status = sl_bt_advertiser_create_set(&adv_handle[HANDLE_IBEACON]);
      if (status) {
        sl_zigbee_app_debug_println("sl_bt_advertiser_create_set status 0x%02x", status);
      }

      status = sl_bt_advertiser_create_set(&adv_handle[HANDLE_EDDYSTONE]);
      if (status) {
        sl_zigbee_app_debug_println("sl_bt_advertiser_create_set status 0x%02x", status);
      }

      // start advertising
      enableBleAdvertisements();
    }
    break;
    case sl_bt_evt_gatt_server_characteristic_status_id: {
      sl_bt_evt_gatt_server_characteristic_status_t *StatusEvt =
        (sl_bt_evt_gatt_server_characteristic_status_t*) &(evt->data);
      if (StatusEvt->status_flags == sl_bt_gatt_server_confirmation) {
        /*
         * DEMO FIX:
         * Do not chain trigger_source -> source_address indications here.
         * The xG26 command response is already sent directly by
         * xg26_ble_send_response_code(). Chaining another indication can fail
         * when the phone did not enable indication/notification on source_address,
         * and it makes the Android app look like the command failed even when
         * Zigbee Network Steering is running correctly.
         */
        sl_zigbee_app_debug_println(
          "GATT confirmation: characteristic= %d , client_config_flags = %d\r\n",
          StatusEvt->characteristic, StatusEvt->client_config_flags);
      } else if (StatusEvt->status_flags == sl_bt_gatt_server_client_config) {
        if (StatusEvt->characteristic == gattdb_light_state) {
          ble_lightState_config = (sl_bt_gatt_client_config_flag_t)StatusEvt->client_config_flags;
        } else if (StatusEvt->characteristic == gattdb_trigger_source) {
          ble_triggerSrc_config = (sl_bt_gatt_client_config_flag_t)StatusEvt->client_config_flags;
        } else if (StatusEvt->characteristic == gattdb_source_address) {
          ble_bleSrc_config = (sl_bt_gatt_client_config_flag_t)StatusEvt->client_config_flags;
        }
        sl_zigbee_app_debug_println(
          "SERVER : ble_lightState_config= %d , ble_triggerSrc_config = %d , ble_bleSrc_config = %d\r\n",
          ble_lightState_config,
          ble_triggerSrc_config,
          ble_bleSrc_config);
      }
    }
    break;
    case sl_bt_evt_connection_opened_id: {
      sl_zigbee_app_debug_println("sl_bt_evt_connection_opened_id \n");
      sl_bt_evt_connection_opened_t *conn_evt =
        (sl_bt_evt_connection_opened_t*) &(evt->data);
      uint8_t index = bleConnectionInfoTableFindUnused();
      if (index == 0xFF) {
        sl_zigbee_app_debug_println("MAX active BLE connections");
        assert(index < 0xFF);
      } else {
        bleConnectionTable[index].inUse = true;
        bleConnectionTable[index].isMaster = (conn_evt->role > 0);
        bleConnectionTable[index].connectionHandle = conn_evt->connection;
        bleConnectionTable[index].bondingHandle = conn_evt->bonding;
        (void) memcpy(bleConnectionTable[index].remoteAddress,
                      conn_evt->address.addr, 6);

        activeBleConnections++;
        //preferred phy 1: 1M phy, 2: 2M phy, 4: 125k coded phy, 8: 500k coded phy
        //accepted phy 1: 1M phy, 2: 2M phy, 4: coded phy, ff: any
        sl_bt_connection_set_preferred_phy(conn_evt->connection, sl_bt_gap_phy_1m, 0xff);
        enableBleAdvertisements();
        sl_zigbee_app_debug_println("BLE connection opened");
        bleConnectionInfoTablePrintEntry(index);
        sl_zigbee_app_debug_println("%d active BLE connection",
                                    activeBleConnections);
      }
    }
    break;
    case sl_bt_evt_connection_phy_status_id: {
      sl_bt_evt_connection_phy_status_t *conn_evt =
        (sl_bt_evt_connection_phy_status_t *)&(evt->data);
      // indicate the PHY that has been selected
      sl_zigbee_app_debug_println("now using the %dMPHY\r\n",
                                  conn_evt->phy);
    }
    break;
    case sl_bt_evt_connection_closed_id: {
      sl_bt_evt_connection_closed_t *conn_evt =
        (sl_bt_evt_connection_closed_t*) &(evt->data);
      uint8_t index = bleConnectionInfoTableLookup(conn_evt->connection);
      assert(index < 0xFF);

      bleConnectionTable[index].inUse = false;
      if ( activeBleConnections ) {
        --activeBleConnections;
      }
      // restart advertising, set connectable
      enableBleAdvertisements();
      if (bleConnectionInfoTableIsEmpty()) {
        sl_dmp_ui_bluetooth_connected(false);
      }
      sl_zigbee_app_debug_println(
        "BLE connection closed, handle=0x%02x, reason=0x%02x : [%d] active BLE connection",
        conn_evt->connection, conn_evt->reason, activeBleConnections);
    }
    break;

    case sl_bt_evt_scanner_legacy_advertisement_report_id: {
      sl_zigbee_app_debug_print("Scan response, address type=0x%02x",
                                evt->data.evt_scanner_legacy_advertisement_report.address_type);
      zb_ble_dmp_print_ble_address(evt->data.evt_scanner_legacy_advertisement_report.address.addr);
      sl_zigbee_app_debug_println("");
    }
    break;

    case sl_bt_evt_connection_parameters_id: {
      sl_bt_evt_connection_parameters_t* param_evt =
        (sl_bt_evt_connection_parameters_t*) &(evt->data);
      sl_zigbee_app_debug_println(
        "BLE connection parameters are updated, handle=0x%02x, interval=0x%04x, latency=0x%04x, timeout=0x%04x, security=0x%02x",
        param_evt->connection,
        param_evt->interval,
        param_evt->latency,
        param_evt->timeout,
        param_evt->security_mode);
      sl_dmp_ui_bluetooth_connected(true);
    }
    break;

    case sl_bt_evt_gatt_service_id: {
      sl_bt_evt_gatt_service_t* service_evt =
        (sl_bt_evt_gatt_service_t*) &(evt->data);
      uint8_t i;
      sl_zigbee_app_debug_println(
        "GATT service, conn_handle=0x%02x, service_handle=0x%04x",
        service_evt->connection, service_evt->service);
      sl_zigbee_app_debug_print("UUID=[");
      for (i = 0; i < service_evt->uuid.len; i++) {
        sl_zigbee_app_debug_print("%02X", service_evt->uuid.data[i]);
      }
      sl_zigbee_app_debug_println("]");
    }
    break;

    default:
      break;
  }
}

// Initialization of all application code
void sli_ble_application_init(uint8_t init_level)
{
  switch (init_level) {
    case SL_ZIGBEE_INIT_LEVEL_EVENT:
    {
      sl_zigbee_af_event_init(attrWriteEvent, attrWriteEventHandler);
      sl_zigbee_af_event_init(xg26RejoinEvent, xg26RejoinEventHandler);
      sl_zigbee_af_event_init(xg26JoinTimeoutEvent, xg26JoinTimeoutEventHandler);
      break;
    }

    case SL_ZIGBEE_INIT_LEVEL_LOCAL_DATA:
    {
      bleConnectionInfoTableInit();
      break;
    }
  }
}

static void xg26RejoinEventHandler(sl_zigbee_af_event_t * event)
{
  (void)event;
  sl_zigbee_af_event_set_inactive(xg26RejoinEvent);

  sl_zigbee_app_debug_println("[XG26] Rejoin delay finished. Starting network steering with option 0...");
  sli_zigbee_af_network_steering_options_mask = 0;
  sl_status_t status = sl_zigbee_af_network_steering_start();
  sl_zigbee_app_debug_println("[XG26] sl_zigbee_af_network_steering_start status = 0x%04X", (unsigned int)status);

  if (xg26_rejoin_connection != 0xFF) {
    if (status == SL_STATUS_OK) {
      xg26_join_in_progress = true;
      sl_zigbee_af_event_set_delay_ms(xg26JoinTimeoutEvent, XG26_JOIN_TIMEOUT_MS);
      if (xg26_rejoin_report_as_join) {
        xg26_ble_send_response_code(xg26_rejoin_connection, XG26_RESP_JOIN_STARTED);
      } else {
        xg26_ble_send_response_code(xg26_rejoin_connection, XG26_RESP_REJOIN_JOIN_STARTED);
      }
    } else {
      if (xg26_rejoin_report_as_join) {
        xg26_ble_send_response_code(xg26_rejoin_connection, XG26_RESP_JOIN_START_FAILED);
      } else {
        xg26_ble_send_response_code(xg26_rejoin_connection, XG26_RESP_REJOIN_JOIN_FAILED);
      }
    }
  }

  xg26_rejoin_connection = 0xFF;
  xg26_rejoin_report_as_join = false;
}

static void attrWriteEventHandler(sl_zigbee_af_event_t * event)
{
  (void)event;
  sl_zigbee_af_event_set_inactive(attrWriteEvent);

  (void) sl_zigbee_af_write_attribute(sl_zigbee_af_primary_endpoint(),
                                      ZCL_ON_OFF_CLUSTER_ID,
                                      ZCL_ON_OFF_ATTRIBUTE_ID,
                                      CLUSTER_MASK_SERVER,
                                      &ble_lightState,
                                      ZCL_BOOLEAN_ATTRIBUTE_TYPE);
}

void zb_ble_dmp_print_ble_connections()
{
  uint8_t i;
  if (bleConnectionInfoTableIsEmpty()) {
    sl_zigbee_app_debug_println("No BLE connections.");
  } else {
    for (i = 0; i < SL_BT_CONFIG_MAX_CONNECTIONS; i++) {
      if (bleConnectionTable[i].inUse) {
        bleConnectionInfoTablePrintEntry(i);
      }
    }
  }
}





static void xg26JoinTimeoutEventHandler(sl_zigbee_af_event_t * event)
{
  (void)event;
  sl_zigbee_af_event_set_inactive(xg26JoinTimeoutEvent);

  if (!xg26_join_in_progress) {
    return;
  }

  if (sl_zigbee_stack_is_up()) {
    xg26_join_in_progress = false;
    xg26_join_retry_count = 0;
    return;
  }

  sl_zigbee_app_debug_println("[XG26] Join timeout. Stopping network steering...");
  sl_status_t status = sl_zigbee_af_network_steering_stop();
  sl_zigbee_app_debug_println("[XG26] sl_zigbee_af_network_steering_stop status = 0x%04X", (unsigned int)status);
  xg26_join_in_progress = false;

  if (xg26_join_retry_count < XG26_JOIN_MAX_RETRIES) {
    xg26_join_retry_count++;
    sl_zigbee_app_debug_println("[XG26] Auto retry join from phone in %d ms (attempt %d/%d)...",
                                XG26_JOIN_RETRY_DELAY_MS,
                                xg26_join_retry_count,
                                XG26_JOIN_MAX_RETRIES);
    xg26_rejoin_report_as_join = true;
    sl_zigbee_af_event_set_delay_ms(xg26RejoinEvent, XG26_JOIN_RETRY_DELAY_MS);
  } else {
    sl_zigbee_app_debug_println("[XG26] Join retry limit reached. Please reopen permit join and press Tham gia again.");
    xg26_join_retry_count = 0;
    xg26_rejoin_connection = 0xFF;
    xg26_rejoin_report_as_join = false;
  }
}

void sl_zigbee_af_network_steering_complete_cb(sl_status_t status,
                                               uint8_t totalBeacons,
                                               uint8_t joinAttempts,
                                               uint8_t finalState)
{
  bool should_retry = (xg26_join_in_progress
                       && status != SL_STATUS_OK
                       && !sl_zigbee_stack_is_up());

  xg26_join_in_progress = false;
  sl_zigbee_af_event_set_inactive(xg26JoinTimeoutEvent);

  sl_zigbee_af_core_println("Network Steering Completed: %s (0x%02X)",
                            (status == SL_STATUS_OK ? "Join Success" : "FAILED"),
                            status);
  sl_zigbee_af_core_println("Finishing state: 0x%02X", finalState);
  sl_zigbee_af_core_println("Beacons heard: %d\nJoin Attempts: %d", totalBeacons, joinAttempts);

  if (status == SL_STATUS_OK) {
    xg26_join_retry_count = 0;
    xg26_rejoin_connection = 0xFF;
    xg26_rejoin_report_as_join = false;
    return;
  }

  if (should_retry && xg26_join_retry_count < XG26_JOIN_MAX_RETRIES) {
    xg26_join_retry_count++;
    sl_zigbee_app_debug_println("[XG26] Network steering failed. Auto retry from phone in %d ms (attempt %d/%d)...",
                                XG26_JOIN_RETRY_DELAY_MS,
                                xg26_join_retry_count,
                                XG26_JOIN_MAX_RETRIES);
    xg26_rejoin_report_as_join = true;
    sl_zigbee_af_event_set_delay_ms(xg26RejoinEvent, XG26_JOIN_RETRY_DELAY_MS);
  } else if (should_retry) {
    sl_zigbee_app_debug_println("[XG26] Join retry limit reached after steering failure. Please reopen permit join and press Tham gia again.");
    xg26_join_retry_count = 0;
    xg26_rejoin_connection = 0xFF;
    xg26_rejoin_report_as_join = false;
  }
}







