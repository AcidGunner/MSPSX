#include <stdint.h>
#include <string.h>
#include <psxetc.h>
#include <psxpad.h>

#include "spi.h"
#include "pad.h"

static volatile uint8_t  pad_buff[2][34];
static volatile size_t   pad_buff_len[2];
static volatile uint32_t pad_config_attempt[2] = { 0, 0 };

static uint16_t held = 0;
static uint16_t pressed = 0;
static uint16_t released = 0;
static uint16_t oldHeld = 0;

void send_pad_cmd(
	uint32_t     port,
	PadCommand   cmd,
	uint8_t      arg1,
	uint8_t      arg2,
	SPI_Callback callback
) {
	SPI_Request *req = SPI_CreateRequest();

	req->len              = 9;
	req->port             = port;
	req->callback         = callback;
	req->pad_req.addr     = 0x01;
	req->pad_req.cmd      = cmd;
	req->pad_req.tap_mode = 0x00;
	req->pad_req.motor_r  = arg1;
	req->pad_req.motor_l  = arg2;

	memset(
		req->pad_req.dummy,
		(cmd == PAD_CMD_REQUEST_CONFIG) ? 0xff : 0x00,
		4
	);
}

void dualshock_init_cb(uint32_t port, const volatile uint8_t *buff, size_t rx_len) {
	PadResponse *pad = (PadResponse *) buff;

	if (
		(rx_len < 2) ||
		(pad->prefix != 0x5a) ||
		(pad->type != PAD_ID_CONFIG_MODE)
	) {
		pad_config_attempt[port]++;
		return;
	}

	send_pad_cmd(port, PAD_CMD_CONFIG_MODE,     0x01, 0x00, 0);
	send_pad_cmd(port, PAD_CMD_SET_ANALOG,      0x01, 0x02, 0);
	send_pad_cmd(port, PAD_CMD_INIT_PRESSURE,   0x00, 0x00, 0); // Ignored by DualShock 1
	send_pad_cmd(port, PAD_CMD_REQUEST_CONFIG,  0x00, 0x01, 0);
	send_pad_cmd(port, PAD_CMD_RESPONSE_CONFIG, 0xff, 0xff, 0); // Ignored by DualShock 1
	send_pad_cmd(port, PAD_CMD_CONFIG_MODE,     0x00, 0x00, 0);
}

void poll_cb(uint32_t port, const volatile uint8_t *buff, size_t rx_len) {
	pad_buff_len[port] = rx_len;
	if (rx_len)
		memcpy((void *) pad_buff[port], (void *) buff, rx_len);

	PadResponse *pad = (PadResponse *) buff;

	if (
		rx_len &&
		((pad->prefix == 0x5a) || !(pad->prefix)) &&
		(pad->type == PAD_ID_DIGITAL)
	) {
		if (pad_config_attempt[port] < 3) {
			send_pad_cmd(port, PAD_CMD_CONFIG_MODE, 0x01, 0x00, 0);
			send_pad_cmd(port, PAD_CMD_READ,        0x00, 0x00, &dualshock_init_cb);
		}
	}
	else pad_config_attempt[port] = 0;
}

void Pad_Init(void)
{
    memset((void *)pad_buff, 0, sizeof(pad_buff));
    memset((void *)pad_buff_len, 0, sizeof(pad_buff_len));
	
	held = 0;
    pressed = 0;
    released = 0;
    oldHeld = 0;
	
    SPI_Init(&poll_cb);
}

void Pad_Update(void)
{
    if (pad_buff_len[0] < 4)
        return;

    PadResponse *pad = (PadResponse *)pad_buff[0];

    held = (uint16_t)~pad->btn;

    pressed = held & ~oldHeld;
    released = oldHeld & ~held;

    oldHeld = held;
}

uint16_t Pad_Held(void)
{
    return held;
}

uint16_t Pad_Pressed(void)
{
    return pressed;
}

uint16_t Pad_Released(void)
{
    return released;
}