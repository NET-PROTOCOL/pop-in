#include "L3_FSMevent.h"
#include "L3_msg.h"
#include "L3_timer.h"
#include "L3_LLinterface.h"
#include "protocol_parameters.h"
#include "mbed.h"

//FSM state -------------------------------------------------
#define L3STATE_SCANNING            0  // 메인 상태 - 네트워크 스캔
#define L3STATE_CONNECTED           1  // 연결된 상태

//Node types
#define NODE_TYPE_USER              0
#define NODE_TYPE_BOOTH             1

//Connection message types
#define L3_MSG_TYPE_BEACON          0x10
#define L3_MSG_TYPE_CONN_REQ        0x11
#define L3_MSG_TYPE_CONN_RESP       0x12
#define L3_MSG_TYPE_DATA            0x20

//state variables
static uint8_t main_state = L3STATE_SCANNING; //protocol state - 초기 상태는 SCANNING
static uint8_t prev_state = main_state;

//SDU (input)
static uint8_t originalWord[1030];
static uint8_t wordLen=0;
static uint8_t sdu[1030];

//Network scanning variables
#define MAX_BOOTH_NODES             10
#define SCAN_TIMEOUT_SEC            5  // 스캔 타임아웃 시간

typedef struct {
    uint8_t nodeId;
    int16_t rssi;
    int8_t snr;
    uint8_t nodeType;
    uint8_t isActive;
} BoothNode_t;

static BoothNode_t detectedBooths[MAX_BOOTH_NODES];
static uint8_t numDetectedBooths = 0;
static uint8_t bestBoothId = 0;
static int16_t bestRssi = -200; // 매우 낮은 초기값

//Connection variables
static uint8_t myNodeType = NODE_TYPE_USER;
static uint8_t connectedBoothId = 0;
static uint8_t isConnected = 0;
static uint8_t connectionRequested = 0;

//Scanning control variables
static uint8_t scanRequested = 0;
static uint8_t scanInProgress = 0;
static uint8_t scanCompleted = 0;

//serial port interface
static Serial pc(USBTX, USBRX);
static uint8_t myNodeId; // dest ID 제거, 노드 ID만 사용

//Beacon message structure
typedef struct {
    uint8_t msgType;
    uint8_t nodeId;
    uint8_t nodeType;
    uint8_t reserved;
} BeaconMsg_t;

//Connection request/response structure
typedef struct {
    uint8_t msgType;
    uint8_t srcId;
    uint8_t destId;
    uint8_t status; // 0: request, 1: accept, 2: reject
} ConnMsg_t;

//application event handler : generating SDU from keyboard input
static void L3service_processInputWord(void)
{
    char c = pc.getc();
    
    // 스캐닝 상태에서 's' 또는 'S' 입력 시 스캔 시작
    if (main_state == L3STATE_SCANNING)
    {
        if (c == 's' || c == 'S')
        {
            if (!scanInProgress)
            {
                scanRequested = 1;
                scanInProgress = 1;
                scanCompleted = 0;
                numDetectedBooths = 0;
                bestBoothId = 0;
                bestRssi = -200;
                
                // 기존 감지된 부스들 초기화
                for (int i = 0; i < MAX_BOOTH_NODES; i++)
                {
                    detectedBooths[i].isActive = 0;
                }
                
                pc.printf("Scanning for booth nodes...\n");
                L3_timer_startTimer(); // 스캔 타이머 시작
            }
        }
        else if ((c == 'y' || c == 'Y') && bestBoothId != 0)
        {
            // 최적 부스가 있고 연결을 원할 때
            connectionRequested = 1;
            L3_event_setEventFlag(L3_event_dataToSend);
        }
        else if ((c == 'n' || c == 'N') && scanCompleted)
        {
            // 스캔 완료 후 재시도 거부
            pc.printf("Scan cancelled. Press 's' to start scanning again.\n");
        }
        return;
    }
    
    // 연결된 상태에서만 메시지 입력 처리
    if (main_state == L3STATE_CONNECTED && !L3_event_checkEventFlag(L3_event_dataToSend))
    {
        if (c == '\n' || c == '\r')
        {
            originalWord[wordLen++] = '\0';
            L3_event_setEventFlag(L3_event_dataToSend);
            debug_if(DBGMSG_L3,"word is ready! ::: %s\n", originalWord);
        }
        else
        {
            originalWord[wordLen++] = c;
            if (wordLen >= L3_MAXDATASIZE-1)
            {
                originalWord[wordLen++] = '\0';
                L3_event_setEventFlag(L3_event_dataToSend);
                pc.printf("\n max reached! word forced to be ready :::: %s\n", originalWord);
            }
        }
    }
}

void L3_sendBeacon(void)
{
    BeaconMsg_t beacon;
    beacon.msgType = L3_MSG_TYPE_BEACON;
    beacon.nodeId = myNodeId;
    beacon.nodeType = myNodeType;
    beacon.reserved = 0;
    
    L3_LLI_dataReqFunc((uint8_t*)&beacon, sizeof(BeaconMsg_t), 255); // 브로드캐스트
}

void L3_sendConnectionRequest(uint8_t boothId)
{
    ConnMsg_t connReq;
    connReq.msgType = L3_MSG_TYPE_CONN_REQ;
    connReq.srcId = myNodeId;
    connReq.destId = boothId;
    connReq.status = 0; // request
    
    L3_LLI_dataReqFunc((uint8_t*)&connReq, sizeof(ConnMsg_t), boothId);
    pc.printf("[INFO] Connection request sent to Booth %d\n", boothId);
}

void L3_sendConnectionResponse(uint8_t userId, uint8_t accept)
{
    ConnMsg_t connResp;
    connResp.msgType = L3_MSG_TYPE_CONN_RESP;
    connResp.srcId = myNodeId;
    connResp.destId = userId;
    connResp.status = accept ? 1 : 2; // 1: accept, 2: reject
    
    L3_LLI_dataReqFunc((uint8_t*)&connResp, sizeof(ConnMsg_t), userId);
}

void L3_addOrUpdateBooth(uint8_t nodeId, int16_t rssi, int8_t snr)
{
    // 기존 부스 노드 업데이트 확인
    for (int i = 0; i < numDetectedBooths; i++)
    {
        if (detectedBooths[i].nodeId == nodeId)
        {
            detectedBooths[i].rssi = rssi;
            detectedBooths[i].snr = snr;
            detectedBooths[i].isActive = 1;
            return;
        }
    }
    
    // 새로운 부스 노드 추가
    if (numDetectedBooths < MAX_BOOTH_NODES)
    {
        detectedBooths[numDetectedBooths].nodeId = nodeId;
        detectedBooths[numDetectedBooths].rssi = rssi;
        detectedBooths[numDetectedBooths].snr = snr;
        detectedBooths[numDetectedBooths].nodeType = NODE_TYPE_BOOTH;
        detectedBooths[numDetectedBooths].isActive = 1;
        numDetectedBooths++;
    }
}

void L3_findBestBooth(void)
{
    bestRssi = -200;
    bestBoothId = 0;
    
    for (int i = 0; i < numDetectedBooths; i++)
    {
        if (detectedBooths[i].isActive && detectedBooths[i].rssi > bestRssi)
        {
            bestRssi = detectedBooths[i].rssi;
            bestBoothId = detectedBooths[i].nodeId;
        }
    }
    
    scanCompleted = 1;
    scanInProgress = 0;
    
    if (bestBoothId != 0)
    {
        pc.printf("\n=== BOOTH FOUND ===\n");
        pc.printf("Best Booth ID: %d\n", bestBoothId);
        pc.printf("Signal Strength: %d dBm\n", bestRssi);
        pc.printf("Do you want to connect? (y/n): ");
    }
    else
    {
        pc.printf("\n=== SCAN COMPLETE ===\n");
        pc.printf("최적 부스노드가 없어요.\n");
        pc.printf("다시 하시겠어요? (s: 재스캔, n: 취소): ");
    }
}

void L3_handleBeaconMessage(uint8_t* dataPtr, uint8_t srcId, int16_t rssi, int8_t snr)
{
    BeaconMsg_t* beacon = (BeaconMsg_t*)dataPtr;
    
    // 스캔 중이고 부스 노드만 처리
    if (scanInProgress && beacon->nodeType == NODE_TYPE_BOOTH)
    {
        debug_if(DBGMSG_L3, "[L3] Booth beacon received from ID %d, RSSI: %d\n", srcId, rssi);
        L3_addOrUpdateBooth(srcId, rssi, snr);
    }
}

void L3_handleConnectionRequest(uint8_t* dataPtr, uint8_t srcId)
{
    ConnMsg_t* connReq = (ConnMsg_t*)dataPtr;
    
    if (myNodeType == NODE_TYPE_BOOTH && !isConnected)
    {
        // 부스가 연결 요청을 받았을 때
        pc.printf("[INFO] Connection request from User %d. Accepting...\n", srcId);
        L3_sendConnectionResponse(srcId, 1); // accept
        connectedBoothId = srcId;
        isConnected = 1;
        main_state = L3STATE_CONNECTED;
    }
}

void L3_handleConnectionResponse(uint8_t* dataPtr, uint8_t srcId)
{
    ConnMsg_t* connResp = (ConnMsg_t*)dataPtr;
    
    if (myNodeType == NODE_TYPE_USER && connResp->status == 1)
    {
        // 사용자가 연결 승인을 받았을 때
        pc.printf("[INFO] Connection accepted by Booth %d!\n", srcId);
        connectedBoothId = srcId;
        isConnected = 1;
        main_state = L3STATE_CONNECTED;
        pc.printf("Connected! You can now send messages.\n");
        pc.printf("Give a word to send : ");
    }
    else if (connResp->status == 2)
    {
        pc.printf("[INFO] Connection rejected by Booth %d\n", srcId);
        connectionRequested = 0;
    }
}

void L3_initFSM(uint8_t userId) // 파라미터명 변경: destId -> userId
{
    myNodeId = userId; // myDestId -> myNodeId로 변경
    
    // 노드 타입 설정 (ID에 따라 구분)
    if (userId >= 100) // ID 100 이상은 부스로 가정
    {
        myNodeType = NODE_TYPE_BOOTH;
        pc.printf("=== BOOTH NODE (ID: %d) ===\n", userId);
        pc.printf("Waiting for user connections...\n");
        
        // 부스는 자동으로 비콘 전송 시작
        L3_timer_startTimer();
    }
    else
    {
        myNodeType = NODE_TYPE_USER;
        pc.printf("=== USER NODE (ID: %d) ===\n", userId);
        pc.printf("Press 's' to start scanning for booth nodes...\n");
    }
    
    //initialize service layer
    pc.attach(&L3service_processInputWord, Serial::RxIrq);
}

void L3_FSMrun(void)
{   
    if (prev_state != main_state)
    {
        debug_if(DBGMSG_L3, "[L3] State transition from %i to %i\n", prev_state, main_state);
        prev_state = main_state;
    }

    //FSM should be implemented here! ---->>>>
    switch (main_state)
    {
        case L3STATE_SCANNING: //SCANNING state (메인 상태)
            
            // 타이머 만료 시 처리
            if (!L3_timer_getTimerStatus())
            {
                if (myNodeType == NODE_TYPE_BOOTH)
                {
                    // 부스는 주기적으로 비콘 전송
                    L3_sendBeacon();
                    L3_timer_startTimer(); // 타이머 재시작
                }
                else if (myNodeType == NODE_TYPE_USER && scanInProgress)
                {
                    // 사용자 스캔 타임아웃
                    L3_findBestBooth();
                    // 스캔 완료 후 타이머 재시작하지 않음
                }
            }
            
            if (L3_event_checkEventFlag(L3_event_msgRcvd)) //if data reception event happens
            {
                //Retrieving data info.
                uint8_t* dataPtr = L3_LLI_getMsgPtr();
                uint8_t size = L3_LLI_getSize();
                uint8_t srcId = L3_LLI_getSrcId();
                int16_t rssi = L3_LLI_getRssi();
                int8_t snr = L3_LLI_getSnr();
                
                uint8_t msgType = dataPtr[0];
                
                switch (msgType)
                {
                    case L3_MSG_TYPE_BEACON:
                        if (myNodeType == NODE_TYPE_USER)
                        {
                            L3_handleBeaconMessage(dataPtr, srcId, rssi, snr);
                        }
                        break;
                        
                    case L3_MSG_TYPE_CONN_REQ:
                        L3_handleConnectionRequest(dataPtr, srcId);
                        break;
                        
                    case L3_MSG_TYPE_CONN_RESP:
                        L3_handleConnectionResponse(dataPtr, srcId);
                        break;
                        
                    default:
                        debug_if(DBGMSG_L3, "[L3] Unknown message type: 0x%02X\n", msgType);
                        break;
                }
                
                L3_event_clearEventFlag(L3_event_msgRcvd);
            }
            else if (L3_event_checkEventFlag(L3_event_dataToSend)) //connection request
            {
                if (myNodeType == NODE_TYPE_USER && connectionRequested && bestBoothId != 0)
                {
                    L3_sendConnectionRequest(bestBoothId);
                    connectionRequested = 0;
                }
                
                L3_event_clearEventFlag(L3_event_dataToSend);
            }
            break;

        case L3STATE_CONNECTED: //CONNECTED state
            
            if (L3_event_checkEventFlag(L3_event_msgRcvd)) //if data reception event happens
            {
                //Retrieving data info.
                uint8_t* dataPtr = L3_LLI_getMsgPtr();
                uint8_t size = L3_LLI_getSize();
                uint8_t srcId = L3_LLI_getSrcId();
                
                uint8_t msgType = dataPtr[0];
                
                if (msgType == L3_MSG_TYPE_DATA)
                {
                    // 데이터 메시지 처리
                    debug("\n -------------------------------------------------\nRCVD MSG from %d: %s (length:%i)\n -------------------------------------------------\n", 
                                srcId, dataPtr + 1, size - 1);
                    
                    if (myNodeType == NODE_TYPE_USER)
                        pc.printf("Give a word to send : ");
                }
                else if (msgType == L3_MSG_TYPE_CONN_REQ)
                {
                    L3_handleConnectionRequest(dataPtr, srcId);
                }
                
                L3_event_clearEventFlag(L3_event_msgRcvd);
            }
            else if (L3_event_checkEventFlag(L3_event_dataToSend)) //if data needs to be sent
            {
                //msg header setting
                sdu[0] = L3_MSG_TYPE_DATA;
                strcpy((char*)sdu + 1, (char*)originalWord);
                debug("[L3] msg length : %i\n", wordLen + 1);
                L3_LLI_dataReqFunc(sdu, wordLen + 1, connectedBoothId);

                debug_if(DBGMSG_L3, "[L3] sending msg to connected booth %d....\n", connectedBoothId);
                wordLen = 0;

                if (myNodeType == NODE_TYPE_USER)
                    pc.printf("Give a word to send : ");

                L3_event_clearEventFlag(L3_event_dataToSend);
            }
            break;

        default :
            break;
    }
}