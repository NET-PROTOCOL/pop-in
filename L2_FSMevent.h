typedef enum L2_event
{
    // 기존 이벤트 유지
    L2_event_dataTxDone = 0,
    L2_event_ackTxDone = 1,
    L2_event_ackRcvd = 2,
    L2_event_dataRcvd = 3,
    L2_event_dataToSend = 4,
    L2_event_arqTimeout = 5,
    L2_event_reconfigSrcId = 6,
    L2_event_dataToSendBuffer = 7,

    // 추가 이벤트
    L2_event_presenceDetected = 8,   // 부스 근처 감지
    L2_event_presenceLost = 9,       // 부스 근처에서 벗어남
    L2_event_broadcastRcvd = 10,     // 브로드캐스트 메시지 수신
    L2_event_queueInfoRcvd = 11,     // 대기열 정보 수신
    L2_event_statusRcvd = 12         // 상태 정보 수신
} L2_event_e;


void L2_event_setEventFlag(L2_event_e event);
void L2_event_clearEventFlag(L2_event_e event);
void L2_event_clearAllEventFlag(void);
int L2_event_checkEventFlag(L2_event_e event);

