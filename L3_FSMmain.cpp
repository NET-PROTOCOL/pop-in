#include "L3_FSMevent.h"
#include "L3_msg.h"
#include "L3_timer.h"
#include "L3_LLinterface.h"
#include "protocol_parameters.h"
#include "mbed.h"


// Pop-in FSM states
#define L3STATE_SCANNING          0  // 부스 스캔 중
#define L3STATE_CONNECTED         1  // 부스와 연결됨
#define L3STATE_WAITING           2  // 대기열에 있음
#define L3STATE_IN_USE            3  // 부스 체험 중

// 사용자 리스트 관리
static uint8_t registeredList[MAX_USERS][ID_SIZE];  // 출석 기록 목록
static uint8_t registeredCount = 0;

static uint8_t activeList[MAX_CAPACITY][ID_SIZE];   // 체험 중인 사용자 목록
static uint8_t activeCount = 0;

static uint8_t waitingList[MAX_WAITING][ID_SIZE];   // 대기 중인 사용자 목록
static uint8_t waitingCount = 0;

// 현재 부스 정보
static uint8_t boothCapacity = 30;    // 부스 최대 수용 인원
static uint8_t boothDescription[DESC_SIZE];         // 부스 설명

//state variables
static uint8_t main_state = L3STATE_SCANNING; //protocol state
static uint8_t prev_state = main_state;

//SDU (input)
static uint8_t originalWord[1030];
static uint8_t wordLen=0;

static uint8_t sdu[1030];

//serial port interface
static Serial pc(USBTX, USBRX);
static uint8_t myUserId = 0;
static uint8_t connectedBoothId = 0;

// 리스트 관리 유틸리티 함수
static int isRegistered(uint8_t* userId)
{
    // registeredList에서 userId 검색
}

static int isActive(uint8_t* userId)
{
    // activeList에서 userId 검색
}

static int isInWaitingList(uint8_t* userId)
{
    // waitingList에서 userId 검색
}

static int isCapacityFull()
{
    return (activeCount >= boothCapacity);
}

static void addToRegisteredList(uint8_t* userId)
{
    // userId를 registeredList에 추가
}

static void addToActiveList(uint8_t* userId)
{
    // userId를 activeList에 추가
}

static void addToWaitingList(uint8_t* userId)
{
    // userId를 waitingList에 추가
}

static void removeFromActiveList(uint8_t* userId)
{
    // userId를 activeList에서 제거
}

static void removeFromWaitingList(uint8_t* userId)
{
    // userId를 waitingList에서 제거
}

//application event handler : generating SDU from keyboard input
static void L3service_processInputWord(void)
{
    char c = pc.getc();
    if (!L3_event_checkEventFlag(L3_event_dataToSend))
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



void L3_initFSM(uint8_t destId)
{

    myUserId = myId;
    //initialize service layer
    pc.attach(&L3service_processInputWord, Serial::RxIrq);

    pc.printf("Give a word to send : ");
}

void L3_FSMrun(void)
{
    // 기존 코드 유지
    if (prev_state != main_state)
    {
        debug_if(DBGMSG_L3, "[L3] State transition from %i to %i\n", prev_state, main_state);
        prev_state = main_state;
    }

    // FSM 구현
    switch (main_state)
    {
        case L3STATE_SCANNING:
            if (L3_event_checkEventFlag(L3_event_userInfoReq))
            {
                // 부스 리스트 정렬, 최적 부스 연결 요청
                // 1, 2번 액션 구현
                L3_event_clearEventFlag(L3_event_userInfoReq);
            }
            else if (L3_event_checkEventFlag(L3_event_connectReq))
            {
                // 부스 연결 확정, 사용자에게 정보 제공
                // 3, 4, 5번 액션 구현
                main_state = L3STATE_CONNECTED;
                L3_event_clearEventFlag(L3_event_connectReq);
            }
            break;

        case L3STATE_CONNECTED:
            if (L3_event_checkEventFlag(L3_event_userResponseYes))
            {
                // 사용자가 YES 응답을 보낸 경우
                if (isRegistered(userId) && !isActive(userId))
                {
                    // 이미 등록된 사용자 - 거부 메시지 전송 및 스캔 상태로 복귀
                    // 6, 1번 액션 구현
                    main_state = L3STATE_SCANNING;
                }
                else if (isCapacityFull() && !isInWaitingList(userId))
                {
                    // 수용 인원 초과 & 대기열에 없음 - 대기열에 추가
                    // 10, 11번 액션 구현
                    main_state = L3STATE_WAITING;
                }
                else if (isCapacityFull() && isInWaitingList(userId))
                {
                    // 수용 인원 초과 & 이미 대기열에 있음 - 현재 위치 알림
                    // 11번 액션 구현
                    main_state = L3STATE_WAITING;
                }
                else if (!isCapacityFull() && !isRegistered(userId))
                {
                    // 입장 가능 & 등록 안됨 - 등록 처리 및 체험 시작
                    // 7, 8, 12번 액션 구현
                    main_state = L3STATE_IN_USE;
                }

                L3_event_clearEventFlag(L3_event_userResponseYes);
            }
            else if (L3_event_checkEventFlag(L3_event_userResponseNo))
            {
                // 사용자가 NO 응답 - 다시 스캔 상태로 전환
                // 1번 액션 구현
                main_state = L3STATE_SCANNING;
                L3_event_clearEventFlag(L3_event_userResponseNo);
            }
            break;

        // WAITING, IN_USE 상태에 대한 추가 구현
        case L3STATE_WAITING:
            // 대기열 상태 처리
            break;

        case L3STATE_IN_USE:
            // 체험 중 상태 처리
            break;

        default:
            break;
    }
}
