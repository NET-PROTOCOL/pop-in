typedef enum L3_event
{
    // 기존 이벤트
    L3_event_msgRcvd = 2,
    L3_event_dataToSend = 4,
    L3_event_dataSendCnf = 5,
    L3_event_recfgSrcIdCnf = 6,

    // Pop-in 프로토콜 이벤트 추가
    L3_event_userInfoReq = 7,
    L3_event_connectReq = 8,
    L3_event_userResponseYes = 9,
    L3_event_userResponseNo = 10,
    L3_event_registerReq = 11,
    L3_event_exitReq = 12,
    L3_event_messageRcvd = 13,
    L3_event_chatMessageRcvd = 14,
    L3_event_timerExpired = 15,
    L3_event_nextUserTurn = 16
} L3_event_e;


void L3_event_setEventFlag(L3_event_e event);
void L3_event_clearEventFlag(L3_event_e event);
void L3_event_clearAllEventFlag(void);
int L3_event_checkEventFlag(L3_event_e event);