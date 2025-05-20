#include "mbed.h"
#include "L3_msg.h"

// 메시지 인코딩/디코딩 함수 추가
uint8_t L3_msg_encodeUserInfo(uint8_t* msg, uint8_t* userId)
{
    msg[0] = L3_MSG_TYPE_USER_INFO;
    memcpy(&msg[1], userId, ID_SIZE);
    return ID_SIZE + 1;
}

uint8_t L3_msg_encodeConnectRequest(uint8_t* msg, uint8_t* userId, uint8_t boothId)
{
    msg[0] = L3_MSG_TYPE_CONNECT;
    memcpy(&msg[1], userId, ID_SIZE);
    msg[ID_SIZE+1] = boothId;
    return ID_SIZE + 2;
}

// 필요한 다른 메시지 인코딩/디코딩 함수 추가