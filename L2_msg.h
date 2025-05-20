#include "mbed.h"

#define L2_MSG_OFFSET_TYPE  0
#define L2_MSG_OFFSET_SEQ   1
#define L2_MSG_OFFSET_DATA  2

#define L2_MSG_ACKSIZE      3

#define L2_MSG_MAXDATASIZE  26
#define L2_MSSG_MAX_SEQNUM  1024
// L2_msg.h에 추가
// 확장된 메시지 타입
#define L2_MSG_TYPE_ACK           0  // 기존 타입 유지
#define L2_MSG_TYPE_DATA          1  // 기존 타입 유지
#define L2_MSG_TYPE_DATA_CONT     2  // 기존 타입 유지
#define L2_MSG_TYPE_BROADCAST     3  // 브로드캐스트 메시지 (관리자 공지)
#define L2_MSG_TYPE_PRESENCE      4  // 사용자 존재 감지 메시지
#define L2_MSG_TYPE_QUEUE_INFO    5  // 대기열 정보 메시지
#define L2_MSG_TYPE_STATUS        6  // 부스 상태 정보 메시지



int L2_msg_checkIfData(uint8_t* msg);
int L2_msg_checkIfAck(uint8_t* msg);
int L2_msg_checkIfEndData(uint8_t* msg);
uint8_t L2_msg_encodeAck(uint8_t* msg_ack, uint8_t seq);
uint8_t L2_msg_encodeData(uint8_t* msg_data, uint8_t* data, int seq, int len, uint8_t);
uint8_t L2_msg_getSeq(uint8_t* msg);
uint8_t* L2_msg_getWord(uint8_t* msg);