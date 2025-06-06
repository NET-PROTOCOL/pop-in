#include "mbed.h"
#include "L3_FSMevent.h" 
#include "L3_msg.h"
#include "L2_LLinterface.h"  // Added to access L2 RSSI/SNR functions
#include "protocol_parameters.h"
#include "time.h"

static uint8_t rcvdMsg[L3_MAXDATASIZE];
static uint8_t rcvdSize;
static int16_t rcvdRssi;
static int8_t rcvdSnr;
static uint8_t rcvdSrcId;

//Downward primitives
//TX function
void (*L3_LLI_dataReqFunc)(uint8_t* msg, uint8_t size, uint8_t destId);
void (*L3_LLI_reconfigSrcIdReqFunc)(uint8_t myId);

//interface event : DATA_IND, RX data has arrived
void L3_LLI_dataInd(uint8_t* dataPtr, uint8_t srcId, uint8_t size, int8_t snr, int16_t rssi)
{
    debug_if(DBGMSG_L3, "\n[L3] --> DATA IND : size:%i, %s from node:%d, RSSI:%d, SNR:%d\n", 
             size, dataPtr, srcId, rssi, snr);

    memcpy(rcvdMsg, dataPtr, size*sizeof(uint8_t));
    rcvdSize = size;
    rcvdSnr = snr;
    rcvdRssi = rssi;
    rcvdSrcId = srcId;

    L3_event_setEventFlag(L3_event_msgRcvd);
}

void L3_LLI_dataCnf(uint8_t res)
{
    debug_if(DBGMSG_L3, "\n --> DATA CNF : res : %i\n", res);
    L3_event_setEventFlag(L3_event_dataSendCnf);
}

void L3_LLI_reconfigSrcIdCnf(uint8_t res)
{
    debug_if(DBGMSG_L3, "\n --> RECONFIG SRCID CNF : res : %i\n", res);
    L3_event_setEventFlag(L3_event_recfgSrcIdCnf);
}

// Getter functions
uint8_t* L3_LLI_getMsgPtr()
{
    return rcvdMsg;
}

uint8_t L3_LLI_getSize()
{
    return rcvdSize;
}

uint8_t L3_LLI_getSrcId()
{
    return rcvdSrcId;
}

// New functions to get RSSI and SNR information
int16_t L3_LLI_getRssi()
{
    return rcvdRssi;
}

int8_t L3_LLI_getSnr()
{
    return rcvdSnr;
}

// Function to get current RSSI and SNR from L2 layer (for real-time info)
int16_t L3_LLI_getCurrentRssi()
{
    return L2_LLI_getRssi();
}

int8_t L3_LLI_getCurrentSnr()
{
    return L2_LLI_getSnr();
}

// Setter functions
void L3_LLI_setDataReqFunc(void (*funcPtr)(uint8_t*, uint8_t, uint8_t))
{
    L3_LLI_dataReqFunc = funcPtr;
}

void L3_LLI_setReconfigSrcIdReqFunc(void (*funcPtr)(uint8_t))
{
    L3_LLI_reconfigSrcIdReqFunc = funcPtr;
}

void L3_LLI_setMsgPtr(uint8_t* ptr, uint8_t size, uint8_t srcId, int16_t rssi, int8_t snr) {
    // 구현 내용
}