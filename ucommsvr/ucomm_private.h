/*
 * Micro Communicator for Projection uC
 * a High-Speed Serial communications server
 *
 * Copyright (C) 2018 AngeloGioacchino Del Regno <kholk11@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* MicroComm Server definitions */
#define UCOMMSERVER_DIR			"/dev/socket/ucommsvr/"
#define UCOMMSERVER_SOCKET		UCOMMSERVER_DIR "ucommsvr"
#define UCOMMSERVER_MAXCONN		10

typedef enum {
	OP_INITIALIZE = 0,
	OP_POWER,
	OP_BRIGHTNESS,
	OP_FOCUS_SET,
	OP_KEYSTONE_SET,
	OP_FOCUS_GET,
	OP_KEYSTONE_GET,
	OP_MAX,
} ucomm_svr_ops_t;

struct micro_communicator_cached_data {
	bool light_suspended;
	uint8_t light;
	uint8_t focus;
	uint8_t keystone;
};

struct micro_communicator_params {
	int32_t operation;
	int32_t value;
};

#define CTYPE_SHORT_STATUS_REPLY	0x02
#define CTYPE_SHORT_DATA_REPLY		0x04
#define CTYPE_LONG_DATA_REPLY		0x0c

#define REPLY_FOCUS_CUSTOM_LEN		3
#define FOCUS_PROCESSING_MAX_PASS	4

				/****  VT    SO   */
static const uint8_t std_header[] = { 0x0b, 0x0e };

				/****  CR    LF    SI   */
static const uint8_t std_footer[] = { 0x0d, 0x0a, 0x0f };

/* Initialization sequence */
static const uint8_t cmd_init_hello[]	= { 0x02, 0x00, 0x02 };
static const uint8_t cmd_init_unk1[]	= { 0x02, 0x01, 0x03 };
static const uint8_t cmd_fan_on[]	= { 0x05, 0x80, 0x01,
					    0x03, 0x7f, 0x08 };
static const uint8_t cmd_init_unk3[]	= { 0x04, 0x55, 0x01,
					    0x01, 0x5b };
static const uint8_t cmd_init_led[]	= { 0x08, 0x20, 0x04,
					    0x00, 0x00, 0x00,
					    0x00, 0x00, 0x2c };

/* Alive commands */
static const uint8_t cmd_ir_sensor_on[]	= { 0x04, 0x19, 0x00,
					    0xdd, 0xfa };
static const uint8_t cmd_ir_sensor_off[]= { 0x03, 0x21, 0x40,
					    0x40, 0x64, 0x0d };
static const uint8_t cmd_light_off[]	= { 0x08, 0x20, 0x04,
					    0x00, 0x03, 0x0a,
					    0x00, 0x00, 0x39 };
static const uint8_t cmd_light_lvl[]	= { 0x08, 0x20, 0x04,
					    0x00, 0x01, 0x00,
					    0x00, 0x00, 0x00 };
static const uint8_t cmd_keystone_val[]	= { 0x05, 0x4b, 0x01,
					    0x00, 0x00, 0x00 };
static const uint8_t cmd_focus_query[]	= { 0x07, 0x28, 0x00,
					    0x00, 0x00, 0x00,
					    0x00, 0x2f };
static const uint8_t cmd_focus_up[]	= { 0x07, 0x28, 0x02,
					    0x00, 0x01, 0x00,
					    0x00, 0x32 };
static const uint8_t cmd_focus_down[]	= { 0x07, 0x28, 0x02,
					    0xff, 0xff, 0x00,
					    0x00, 0x2f };

/* Replies */
static const uint8_t cmd_reply_light_ok[]    = { 0x58, 0x5a };	/* XZ */
static const uint8_t cmd_reply_focus_up_ok[] = { 0x00, 0x01 };
static const uint8_t cmd_reply_focus_dn_ok[] = { 0xff, 0xff };
static const uint8_t cmd_reply_unknown[]     = { 0x44, 0x46 };	/* DF */
static const uint8_t cmd_reply_sz_mismatch[] = { 0x51, 0x53 };	/* QS */
static const uint8_t cmd_reply_bad_params[]  = { 0x4e, 0x50 };	/* NP */
static const uint8_t cmd_reply_nul[]         = { 0x00, 0x00 };
static const int cmd_reply_len = 2;


