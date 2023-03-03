/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Definitions from Linux bluetooth directory that are useful for
 * bluetooth audio.
 * TODO(hychao): Remove this file when there is bluetooth user
 * space header provided.
 */

#ifndef CRAS_SRC_COMMON_BLUETOOTH_H_
#define CRAS_SRC_COMMON_BLUETOOTH_H_

#include <unistd.h>

#define HCI_VIRTUAL 0
#define HCI_USB 1
#define HCI_PCCARD 2
#define HCI_UART 3
#define HCI_RS232 4
#define HCI_PCI 5
#define HCI_SDIO 6
#define HCI_BUS_MAX 7

#define BTPROTO_HCI 1
#define BTPROTO_SCO 2

#define SCO_OPTIONS 0x01
#define SCO_CONNINFO 0x02
#define SOL_SCO 17

#define HCIGETDEVINFO _IOR('H', 211, int)

typedef struct {
  uint8_t b[6];
} __attribute__((packed)) bdaddr_t;

struct hci_dev_stats {
  uint32_t err_rx;
  uint32_t err_tx;
  uint32_t cmd_tx;
  uint32_t evt_rx;
  uint32_t acl_tx;
  uint32_t acl_rx;
  uint32_t sco_tx;
  uint32_t sco_rx;
  uint32_t byte_rx;
  uint32_t byte_tx;
};

struct hci_dev_info {
  uint16_t dev_id;
  char name[8];
  bdaddr_t bdaddr;
  uint32_t flags;
  uint8_t type;
  uint8_t features[8];
  uint32_t pkt_type;
  uint32_t link_policy;
  uint32_t link_mode;
  uint16_t acl_mtu;
  uint16_t acl_pkts;
  uint16_t sco_mtu;
  uint16_t sco_pkts;
  struct hci_dev_stats stat;
};

struct sco_options {
  uint16_t mtu;
};

struct sco_conninfo {
  uint16_t hci_handle;
  uint8_t dev_class[3];
};

#define SOL_BLUETOOTH 274

#define BT_VOICE 11
struct bt_voice {
  uint16_t setting;
};

#define BT_VOICE_TRANSPARENT 0x0003

#define BT_SNDMTU 12

#define BT_RCVMTU 13

#define BT_PKT_STATUS 16

#define BT_CODEC 19
struct bt_codec {
  uint8_t id;
  uint16_t cid;
  uint16_t vid;
  uint8_t data_path_id;
  uint8_t num_caps;
  struct codec_caps {
    uint8_t len;
    uint8_t data[];
  } caps[];
} __attribute__((packed));

struct bt_codecs {
  uint8_t num_codecs;
  struct bt_codec codecs[];
} __attribute__((packed));

/* Per BLUETOOTH CORE SPECIFICATION Version 5.2 | Vol 4, Part E, Host Controller
 * Interface Functional Specification, 7.3.101 Configure Data Path command:
 * The Data_Path_ID parameter shall indicate the logical transport channel
 * number to be configured. Note that 0x01 ~ 0xFE is vendor-specific.
 */
#define HCI_CONFIG_DATA_PATH_ID_DEFAULT 0x00
#define HCI_CONFIG_DATA_PATH_ID_OFFLOAD 0x01

/* Per BLUETOOTH CORE SPECIFICATION Version 5.2 | Vol 4, Part E, Host Controller
 * Interface Functional Specification,
 * 7.4.10 Read Local Supported Codec Capabilities command:
 * The first octet of Codec_ID parameter shall indicate the coding format
 * defined in HCI Assigned Numbers.
 */
#define HCI_CONFIG_CODEC_ID_FORMAT_CVSD 0x02
#define HCI_CONFIG_CODEC_ID_FORMAT_MSBC 0x05

#define BT_SCM_PKT_STATUS 0x03

#endif
