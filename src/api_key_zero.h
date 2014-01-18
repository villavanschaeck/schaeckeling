/**************************************************
 Include the API keys. If no API key available,
 set all values to zero.
 The API Key is LSB First: so if it says 11223344,
 define it as APIKey[] = {0x44,0x33,0x22,0x11};
**************************************************/

static unsigned char APIKey[] = {0x00, 0x00, 0x00, 0x00};

/**************************************************
 Port2 assignment for labels present in both APIs. 
**************************************************/
#define GET_WIDGET_PARAMS_2             0
#define GET_WIDGET_PARAMS_REPLY_2       0
#define SET_WIDGET_PARAMS_2             0
#define RECEIVE_DMX_2                   0
#define SEND_DMX_2                      0
#define SEND_DMX_RDM_TX_2               0
#define RECEIVE_DMX_ON_CHANGE_2         0
#define RECEIVED_DMX_COS_TYPE_2         0
#define RDM_DISCOVERY_REQUEST_2         0
#define RDM_CONTROLLER_RECV_TIMEOUT_2   0

/**************************************************
 Assignment for labels present in API2. 
**************************************************/
#define GET_PORT_ASSIGNMENT         0
#define GET_PORT_ASSIGNMENT_REPLY   0
#define SET_PORT_ASSIGNMENT         0
#define RECEIVED_MIDI               0
#define SEND_MIDI                   0
#define SHOW_QUERY                  0
#define SHOW_QUERY_REPLY            0
#define SHOW_ERASE                  0
#define SHOW_WRITE                  0
#define SHOW_READ                   0
#define SHOW_READ_REPLY             0
#define START_SHOW                  0
#define STOP_SHOW                   0

