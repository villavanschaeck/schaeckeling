#define ENTTEC_DMX_USB_PRO              1
#define ENTTEC_DMX_USB_PRO_MK2          2

/**************************************************
 Common API definitions.
**************************************************/
#define GET_WIDGET_SN       10
#define SET_API_KEY         13
#define HARDWARE_VERSION    14

/**************************************************
 API1 definitions.
**************************************************/
#define GET_WIDGET_PARAMS_1             3
#define GET_WIDGET_PARAMS_REPLY_1       3
#define SET_WIDGET_PARAMS_1             4
#define RECEIVE_DMX_1                   5
#define SEND_DMX_1                      6
#define SEND_DMX_RDM_TX_1               7
#define RECEIVE_DMX_ON_CHANGE_1         8
#define RECEIVED_DMX_COS_TYPE_1         9
#define RDM_DISCOVERY_REQUEST_1         11
#define RDM_CONTROLLER_RECV_TIMEOUT_1   12

/**************************************************
 API2 definitions.
 Include the API keys. If no API key available,
 set all values to zero.
**************************************************/
/* #include "api_key_zeroes.h" */
#include "api_key.h"

/*************************************************/

#define MSG_START_CODE 0x7E 
#define MSG_END_CODE 0xE7 
#define OFFSET 0xFF
#define MSG_HEADER_LENGTH 4
#define MSG_END_CODE_LENGTH 1
#define BYTE_LENGTH 8
#define HEADER_RDM_LABEL 5
#define NO_RESPONSE 0

#define DMX_PACKET_SIZE 512

#define RX_BUFFER_SIZE 40960
#define TX_BUFFER_SIZE 40960

#pragma pack(1)
typedef struct {
	unsigned char FirmwareLSB;
	unsigned char FirmwareMSB;
	unsigned char BreakTime;
	unsigned char MaBTime;
	unsigned char RefreshRate;
} DMXUSBPROParamsType;

typedef struct {
	unsigned char UserSizeLSB;
	unsigned char UserSizeMSB;
	unsigned char BreakTime;
	unsigned char MaBTime;
	unsigned char RefreshRate;
} DMXUSBPROSetParamsType;
#pragma pack()

struct ReceivedDmxCosStruct
{
	unsigned char start_changed_byte_number;
	unsigned char changed_byte_array[5];
	unsigned char changed_byte_data[40];
};

struct application_message {
	unsigned char label;
	int length;
	unsigned char data[600];
};

struct mk2_pro_context {
	struct ftdi_context *ftdic;
	dmx_update_callback_t update_callback;
	dmx_commit_callback_t commit_callback;
	dmx_error_callback_t error_callback;
	pthread_t readid;
	unsigned char running;
	char device_type;
};


/**************************************************
 Unclear if these defines are actually required.
**************************************************/

#define MAX_PROS 20
#define SEND_NOW 0
#define TRUE 1
#define FALSE 0
#define HEAD 0
#define IO_ERROR 9

