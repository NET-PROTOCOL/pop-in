#include "L3_FSMevent.h"
#include "L3_msg.h"
#include "L3_timer.h"
#include "L3_LLinterface.h"
#include "L3_admin.h"
#include "protocol_parameters.h"
#include "mbed.h"

//FSM state -------------------------------------------------
#define L3STATE_SCANNING            0  // 메인 상태 - 네트워크 스캔
#define L3STATE_CONNECTED           1  // 연결된 상태
#define L3STATE_IN_USE              2  // 부스 체험 중 상태 (단체 채팅)
#define L3STATE_WAITING             3  // 대기 상태 (waiting queue)


//Node types
#define NODE_TYPE_USER              0
#define NODE_TYPE_BOOTH             1

//Connection message types
#define L3_MSG_TYPE_BEACON          0x10
#define L3_MSG_TYPE_CONN_REQ        0x11
#define L3_MSG_TYPE_CONN_RESP       0x12
#define L3_MSG_TYPE_DATA            0x20
#define L3_MSG_TYPE_ANNOUNCEMENT    0x30
#define L3_MSG_TYPE_BROADCAST       0x40
#define L3_MSG_TYPE_EXPERIENCE_REQ  0x50
#define L3_MSG_TYPE_EXPERIENCE_RESP 0x51

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

//Experience variables
static uint8_t experienceRequested = 0;
static uint8_t inExperience = 0;

//Booth capacity management
#define MAX_BOOTH_CAPACITY          5
static uint8_t connectedUsers[MAX_BOOTH_CAPACITY];
static uint8_t experienceUsers[MAX_BOOTH_CAPACITY];
static uint8_t numConnectedUsers = 0;
static uint8_t numExperienceUsers = 0;

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

//Experience request/response structure
typedef struct {
    uint8_t msgType;
    uint8_t srcId;
    uint8_t destId;
    uint8_t status; // 0: request, 1: accept, 2: reject (capacity full)
} ExperienceMsg_t;

//Broadcast message structure
typedef struct {
    uint8_t msgType;
    uint8_t srcId;
    uint8_t messageLength;
    // message data follows
} BroadcastMsg_t;

//Helper functions for booth capacity management
void L3_addConnectedUser(uint8_t userId)
{
    if (numConnectedUsers < MAX_BOOTH_CAPACITY)
    {
        connectedUsers[numConnectedUsers] = userId;
        numConnectedUsers++;
    }
}

void L3_removeConnectedUser(uint8_t userId)
{
    for (int i = 0; i < numConnectedUsers; i++)
    {
        if (connectedUsers[i] == userId)
        {
            // Shift remaining users
            for (int j = i; j < numConnectedUsers - 1; j++)
            {
                connectedUsers[j] = connectedUsers[j + 1];
            }
            numConnectedUsers--;
            break;
        }
    }
}

void L3_addExperienceUser(uint8_t userId)
{
    if (numExperienceUsers < MAX_BOOTH_CAPACITY)
    {
        experienceUsers[numExperienceUsers] = userId;
        numExperienceUsers++;
    }
}

void L3_removeExperienceUser(uint8_t userId)
{
    for (int i = 0; i < numExperienceUsers; i++)
    {
        if (experienceUsers[i] == userId)
        {
            // Shift remaining users
            for (int j = i; j < numExperienceUsers - 1; j++)
            {
                experienceUsers[j] = experienceUsers[j + 1];
            }
            numExperienceUsers--;
            break;
        }
    }
}

uint8_t L3_isUserInExperience(uint8_t userId)
{
    for (int i = 0; i < numExperienceUsers; i++)
    {
        if (experienceUsers[i] == userId)
        {
            return 1;
        }
    }
    return 0;
}

//application event handler : generating SDU from keyboard input
static void L3service_processInputWord(void)
{
    char c = pc.getc();
    
    // 부스 노드에서 관리자 명령어 처리
    if (myNodeType == NODE_TYPE_BOOTH && L3_admin_getStatus() == 1) // ADMIN_MODE_ACTIVE
    {
        L3_admin_processInput(c);
        
        // 명령어가 준비되면 처리
        if (L3_admin_isCommandReady())
        {
            char* command = L3_admin_getCommand();
            L3_admin_processCommand(command);
        }
        return;
    }
    
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
    
    // 연결된 상태에서 체험 의사 확인
    if (main_state == L3STATE_CONNECTED && myNodeType == NODE_TYPE_USER)
    {
        if (c == 'y' || c == 'Y')
        {
            experienceRequested = 1;
            L3_event_setEventFlag(L3_event_dataToSend);
            return;
        }
        else if (c == 'n' || c == 'N')
        {
            pc.printf("Experience declined. You can still send individual messages.\n");
            pc.printf("Give a word to send : ");
            return;
        }
    }
    
    // 체험 중(IN_USE) 또는 연결된 상태에서 메시지 입력 처리
    if ((main_state == L3STATE_IN_USE || main_state == L3STATE_CONNECTED) && 
        !L3_event_checkEventFlag(L3_event_dataToSend))
    {
        if (c == '\n' || c == '\r')
        {
            originalWord[wordLen++] = '\0';
            L3_event_setEventFlag(L3_event_dataToSend);
            
            if (main_state == L3STATE_IN_USE)
            {
                debug_if(DBGMSG_L3,"broadcast message ready! ::: %s\n", originalWord);
            }
            else
            {
                debug_if(DBGMSG_L3,"word is ready! ::: %s\n", originalWord);
            }
        }
        else
        {
            originalWord[wordLen++] = c;
            if (wordLen >= L3_MAXDATASIZE-1)
            {
                originalWord[wordLen++] = '\0';
                L3_event_setEventFlag(L3_event_dataToSend);
                
                if (main_state == L3STATE_IN_USE)
                {
                    pc.printf("\n max reached! broadcast message forced to be ready :::: %s\n", originalWord);
                }
                else
                {
                    pc.printf("\n max reached! word forced to be ready :::: %s\n", originalWord);
                }
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

void L3_resetConnectionState(void)
{
    connectionRequested = 0;
    isConnected = 0;
    connectedBoothId = 0;
    experienceRequested = 0;
    inExperience = 0;
    
    // 하위 레이어 시퀀스 번호도 리셋 (하위 레이어 함수 필요)
    // L3_LLI_resetSequenceNumber(); // 이런 함수가 있다면
}

void L3_sendConnectionRequest(uint8_t boothId)
{
    // 연결 요청 전 상태 초기화
    L3_resetConnectionState();
    
    ConnMsg_t connReq;
    connReq.msgType = L3_MSG_TYPE_CONN_REQ;
    connReq.srcId = myNodeId;
    connReq.destId = boothId;
    connReq.status = 0;
    
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

void L3_sendExperienceRequest(uint8_t boothId)
{
    ExperienceMsg_t expReq;
    expReq.msgType = L3_MSG_TYPE_EXPERIENCE_REQ;
    expReq.srcId = myNodeId;
    expReq.destId = boothId;
    expReq.status = 0; // request
    
    L3_LLI_dataReqFunc((uint8_t*)&expReq, sizeof(ExperienceMsg_t), boothId);
    pc.printf("[INFO] Experience request sent to Booth %d\n", boothId);
}

void L3_sendExperienceResponse(uint8_t userId, uint8_t accept)
{
    ExperienceMsg_t expResp;
    expResp.msgType = L3_MSG_TYPE_EXPERIENCE_RESP;
    expResp.srcId = myNodeId;
    expResp.destId = userId;
    expResp.status = accept ? 1 : 2; // 1: accept, 2: reject (capacity full)
    
    L3_LLI_dataReqFunc((uint8_t*)&expResp, sizeof(ExperienceMsg_t), userId);
}

void L3_sendBroadcastMessage(uint8_t* message, uint8_t messageLen)
{
    // 체험 중인 모든 사용자에게 브로드캐스트
    for (int i = 0; i < numExperienceUsers; i++)
    {
        uint8_t broadcastMsg[L3_MAXDATASIZE];
        BroadcastMsg_t* header = (BroadcastMsg_t*)broadcastMsg;
        
        header->msgType = L3_MSG_TYPE_BROADCAST;
        header->srcId = myNodeId;
        header->messageLength = messageLen;
        memcpy(broadcastMsg + sizeof(BroadcastMsg_t), message, messageLen);
        
        L3_LLI_dataReqFunc(broadcastMsg, sizeof(BroadcastMsg_t) + messageLen, experienceUsers[i]);
    }
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
    
    if (myNodeType == NODE_TYPE_BOOTH && numConnectedUsers < MAX_BOOTH_CAPACITY)
    {
        // 부스가 연결 요청을 받았을 때 (수용 인원 확인)
        pc.printf("[INFO] Connection request from User %d. Accepting...\n", srcId);
        L3_sendConnectionResponse(srcId, 1); // accept
        L3_addConnectedUser(srcId);
        
        // 관리자 시스템에 사용자 추가
        if (L3_admin_getStatus() == 1) // ADMIN_MODE_ACTIVE
        {
            L3_admin_addUser(srcId, 0, 0); // RSSI와 SNR은 연결 요청에서 가져올 수 없으므로 0으로 설정
        }
    }
    else if (myNodeType == NODE_TYPE_BOOTH)
    {
        // 수용 인원 초과
        pc.printf("[INFO] Connection request from User %d. Rejecting (capacity full)...\n", srcId);
        L3_sendConnectionResponse(srcId, 2); // reject
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
        pc.printf("Connected! Do you want to experience the booth? (y/n): ");
    }
    else if (connResp->status == 2)
    {
        pc.printf("[INFO] Connection rejected by Booth %d (may be full)\n", srcId);
        L3_resetConnectionState(); // 완전한 상태 리셋
        main_state = L3STATE_SCANNING; // 스캐닝 상태로 복귀
    }
}

void L3_handleExperienceRequest(uint8_t* dataPtr, uint8_t srcId)
{
    ExperienceMsg_t* expReq = (ExperienceMsg_t*)dataPtr;
    
    if (myNodeType == NODE_TYPE_BOOTH && numExperienceUsers < MAX_BOOTH_CAPACITY)
    {
        // 부스가 체험 요청을 받았을 때 (수용 인원 확인)
        pc.printf("[INFO] Experience request from User %d. Accepting...\n", srcId);
        L3_sendExperienceResponse(srcId, 1); // accept
        L3_addExperienceUser(srcId);
    }
    else if (myNodeType == NODE_TYPE_BOOTH)
    {
        // 수용 인원 초과
        pc.printf("[INFO] Experience request from User %d. Rejecting (capacity full)...\n", srcId);
        L3_sendExperienceResponse(srcId, 2); // reject
    }
}

void L3_handleExperienceResponse(uint8_t* dataPtr, uint8_t srcId)
{
    ExperienceMsg_t* expResp = (ExperienceMsg_t*)dataPtr;
    
    if (myNodeType == NODE_TYPE_USER && expResp->status == 1)
    {
        // 사용자가 체험 승인을 받았을 때
        pc.printf("[INFO] Experience accepted by Booth %d!\n", srcId);
        inExperience = 1;
        main_state = L3STATE_IN_USE;
        pc.printf("=== BOOTH EXPERIENCE STARTED ===\n");
        pc.printf("You are now in group chat mode. Send messages to all participants:\n");
        pc.printf("Enter message: ");
    }
    else if (expResp->status == 2)
    {
        pc.printf("[INFO] Experience rejected by Booth %d (capacity full)\n", srcId);
        pc.printf("You can still send individual messages to the booth.\n");
        pc.printf("Give a word to send : ");
        experienceRequested = 0;
    }
}

void L3_handleBroadcastMessage(uint8_t* dataPtr, uint8_t srcId)
{
    BroadcastMsg_t* broadcastMsg = (BroadcastMsg_t*)dataPtr;
    char* message = (char*)(dataPtr + sizeof(BroadcastMsg_t));
    
    pc.printf("\n[BROADCAST from %s %d]: %.*s\n", 
              (srcId >= 100) ? "Booth" : "User", 
              srcId, 
              broadcastMsg->messageLength, 
              message);
    
    if (main_state == L3STATE_IN_USE)
    {
        pc.printf("Enter message: ");
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
        
        // 부스 관리자 시스템 초기화 및 활성화
        L3_admin_init(userId, MAX_BOOTH_CAPACITY); // 부스 용량 설정
        L3_admin_activate();
        
        pc.printf("Booth capacity: %d users\n", MAX_BOOTH_CAPACITY);
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
                        
                    case L3_MSG_TYPE_ANNOUNCEMENT:
                        if (myNodeType == NODE_TYPE_USER)
                        {
                            // 공지 메시지 처리
                            uint8_t announcementLength = dataPtr[2];
                            char* message = (char*)(dataPtr + 3);
                            pc.printf("\n[ANNOUNCEMENT from Booth %d]: %.*s\n", srcId, announcementLength, message);
                        }
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
                
                switch (msgType)
                {
                    case L3_MSG_TYPE_DATA:
                        // 데이터 메시지 처리
                        debug("\n -------------------------------------------------\nRCVD MSG from %d: %s (length:%i)\n -------------------------------------------------\n", 
                                    srcId, dataPtr + 1, size - 1);
                        
                        if (myNodeType == NODE_TYPE_USER)
                            pc.printf("Give a word to send : ");
                        break;
                        
                    case L3_MSG_TYPE_CONN_REQ:
                        L3_handleConnectionRequest(dataPtr, srcId);
                        break;
                        
                    case L3_MSG_TYPE_EXPERIENCE_REQ:
                        L3_handleExperienceRequest(dataPtr, srcId);
                        break;
                        
                    case L3_MSG_TYPE_EXPERIENCE_RESP:
                        L3_handleExperienceResponse(dataPtr, srcId);
                        break;
                        
                    case L3_MSG_TYPE_ANNOUNCEMENT:
                        // 연결된 상태에서도 공지 메시지 처리
                        if (myNodeType == NODE_TYPE_USER)
                        {
                            uint8_t announcementLength = dataPtr[2];
                            char* message = (char*)(dataPtr + 3);
                            pc.printf("\n[ANNOUNCEMENT from Booth %d]: %.*s\n", srcId, announcementLength, message);
                            pc.printf("Give a word to send : ");
                        }
                        break;
                        
                    default:
                        debug_if(DBGMSG_L3, "[L3] Unknown message type in CONNECTED: 0x%02X\n", msgType);
                        break;
                }
                
                L3_event_clearEventFlag(L3_event_msgRcvd);
            }
            else if (L3_event_checkEventFlag(L3_event_dataToSend)) //if data needs to be sent
            {
                if (myNodeType == NODE_TYPE_USER && experienceRequested)
                {
                    // 체험 요청 전송
                    L3_sendExperienceRequest(connectedBoothId);
                    experienceRequested = 0;
                }
                else if (wordLen > 0) //일반 메시지 전송
                {
                    // 메시지 준비
                    sdu[0] = L3_MSG_TYPE_DATA;
                    memcpy(sdu + 1, originalWord, wordLen);
                    
                    if (myNodeType == NODE_TYPE_USER && isConnected)
                    {
                        // 사용자가 부스에게 개별 메시지 전송
                        L3_LLI_dataReqFunc(sdu, wordLen + 1, connectedBoothId);
                        debug_if(DBGMSG_L3, "[L3] Message sent to Booth %d: %s\n", connectedBoothId, originalWord);
                    }
                    else if (myNodeType == NODE_TYPE_BOOTH && numConnectedUsers > 0)
                    {
                        // 부스가 연결된 사용자들에게 메시지 전송
                        for (int i = 0; i < numConnectedUsers; i++)
                        {
                            L3_LLI_dataReqFunc(sdu, wordLen + 1, connectedUsers[i]);
                        }
                        debug_if(DBGMSG_L3, "[L3] Message sent to %d connected users: %s\n", numConnectedUsers, originalWord);
                    }
                    
                    // 입력 버퍼 초기화
                    wordLen = 0;
                    memset(originalWord, 0, sizeof(originalWord));
                    
                    if (myNodeType == NODE_TYPE_USER)
                        pc.printf("Give a word to send : ");
                }
                
                L3_event_clearEventFlag(L3_event_dataToSend);
            }
            break;

        case L3STATE_IN_USE: //IN_USE state (부스 체험 중)
            
            if (L3_event_checkEventFlag(L3_event_msgRcvd)) //if data reception event happens
            {
                //Retrieving data info.
                uint8_t* dataPtr = L3_LLI_getMsgPtr();
                uint8_t size = L3_LLI_getSize();
                uint8_t srcId = L3_LLI_getSrcId();
                
                uint8_t msgType = dataPtr[0];
                
                switch (msgType)
                {
                    case L3_MSG_TYPE_DATA:
                        // 개별 데이터 메시지 처리
                        debug("\n -------------------------------------------------\nRCVD MSG from %d: %s (length:%i)\n -------------------------------------------------\n", 
                                    srcId, dataPtr + 1, size - 1);
                        
                        if (myNodeType == NODE_TYPE_USER)
                            pc.printf("Enter message: ");
                        break;
                        
                    case L3_MSG_TYPE_BROADCAST:
                        L3_handleBroadcastMessage(dataPtr, srcId);
                        break;
                        
                    case L3_MSG_TYPE_CONN_REQ:
                        // 체험 중에도 새로운 연결 요청 처리 (부스만)
                        if (myNodeType == NODE_TYPE_BOOTH)
                        {
                            L3_handleConnectionRequest(dataPtr, srcId);
                        }
                        break;
                        
                    case L3_MSG_TYPE_EXPERIENCE_REQ:
                        // 체험 중에도 새로운 체험 요청 처리 (부스만)
                        if (myNodeType == NODE_TYPE_BOOTH)
                        {
                            L3_handleExperienceRequest(dataPtr, srcId);
                        }
                        break;
                        
                    case L3_MSG_TYPE_ANNOUNCEMENT:
                        // 체험 중에도 공지 메시지 처리
                        if (myNodeType == NODE_TYPE_USER)
                        {
                            uint8_t announcementLength = dataPtr[2];
                            char* message = (char*)(dataPtr + 3);
                            pc.printf("\n[ANNOUNCEMENT from Booth %d]: %.*s\n", srcId, announcementLength, message);
                            pc.printf("Enter message: ");
                        }
                        break;
                        
                    default:
                        debug_if(DBGMSG_L3, "[L3] Unknown message type in IN_USE: 0x%02X\n", msgType);
                        break;
                }
                
                L3_event_clearEventFlag(L3_event_msgRcvd);
            }
            else if (L3_event_checkEventFlag(L3_event_dataToSend)) //브로드캐스트 메시지 전송
            {
                if (wordLen > 0)
                {
                    if (myNodeType == NODE_TYPE_USER && inExperience)
                    {
                        // 사용자가 부스에게 브로드캐스트 요청
                        sdu[0] = L3_MSG_TYPE_BROADCAST;
                        sdu[1] = myNodeId; // srcId
                        sdu[2] = wordLen; // messageLength
                        memcpy(sdu + 3, originalWord, wordLen);
                        
                        L3_LLI_dataReqFunc(sdu, wordLen + 3, connectedBoothId);
                        debug_if(DBGMSG_L3, "[L3] Broadcast message sent to Booth %d: %s\n", connectedBoothId, originalWord);
                        
                        pc.printf("Enter message: ");
                    }
                    else if (myNodeType == NODE_TYPE_BOOTH && numExperienceUsers > 0)
                    {
                        // 부스가 체험 중인 모든 사용자에게 브로드캐스트
                        L3_sendBroadcastMessage(originalWord, wordLen);
                        debug_if(DBGMSG_L3, "[L3] Broadcast message sent to %d experience users: %s\n", numExperienceUsers, originalWord);
                    }
                    
                    // 입력 버퍼 초기화
                    wordLen = 0;
                    memset(originalWord, 0, sizeof(originalWord));
                }
                
                L3_event_clearEventFlag(L3_event_dataToSend);
            }
            break;

        default:
            debug_if(DBGMSG_L3, "[L3] Unknown state: %d\n", main_state);
            break;
    }
}

//data reception FSM event
void L3_recvDataFromLowerLayer(uint8_t* ptr, uint8_t size, uint8_t srcId, int16_t rssi, int8_t snr)
{
    debug_if(DBGMSG_L3, "[L3] Received data from node %d, size: %d, RSSI: %d, SNR: %d\n", 
             srcId, size, rssi, snr);
}

// 관리자 시스템을 위한 추가 함수들
void L3_admin_sendAnnouncement(char* message, uint8_t messageLen)
{
    if (myNodeType == NODE_TYPE_BOOTH && numConnectedUsers > 0)
    {
        // 공지 메시지 구조: [msgType][srcId][messageLength][message]
        uint8_t announcementMsg[L3_MAXDATASIZE];
        announcementMsg[0] = L3_MSG_TYPE_ANNOUNCEMENT;
        announcementMsg[1] = myNodeId;
        announcementMsg[2] = messageLen;
        memcpy(announcementMsg + 3, message, messageLen);
        
        // 연결된 모든 사용자에게 공지 전송
        for (int i = 0; i < numConnectedUsers; i++)
        {
            L3_LLI_dataReqFunc(announcementMsg, messageLen + 3, connectedUsers[i]);
        }
        
        pc.printf("[ADMIN] Announcement sent to %d users: %.*s\n", numConnectedUsers, messageLen, message);
    }
}

uint8_t L3_admin_getConnectedUserCount(void)
{
    return numConnectedUsers;
}

uint8_t L3_admin_getExperienceUserCount(void)
{
    return numExperienceUsers;
}

uint8_t* L3_admin_getConnectedUsers(void)
{
    return connectedUsers;
}

uint8_t* L3_admin_getExperienceUsers(void)
{
    return experienceUsers;
}

void L3_admin_disconnectUser(uint8_t userId)
{
    // 연결된 사용자 목록에서 제거
    L3_removeConnectedUser(userId);
    
    // 체험 중인 사용자 목록에서도 제거
    L3_removeExperienceUser(userId);
    
    pc.printf("[ADMIN] User %d has been disconnected\n", userId);
}

void L3_admin_kickUserFromExperience(uint8_t userId)
{
    // 체험 중인 사용자 목록에서만 제거
    L3_removeExperienceUser(userId);
    
    pc.printf("[ADMIN] User %d has been removed from experience\n", userId);
}