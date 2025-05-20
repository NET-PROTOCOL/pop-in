#include "mbed.h"
#include "L3_FSMevent.h"
#include "protocol_parameters.h"


// 다중 타이머 관리
#define MAX_TIMERS      MAX_CAPACITY
static Timeout userTimers[MAX_TIMERS];
static uint8_t timerStatus[MAX_TIMERS];
static uint8_t timerUserIds[MAX_TIMERS][ID_SIZE];

// 타이머 만료 핸들러
void L3_timer_timeoutHandler(void* userData)
{
    uint8_t* userId = (uint8_t*)userData;

    // 해당 userId를 activeList에서 찾아 이벤트 생성
    // userId와 관련된 타이머 상태 변경
    L3_event_setEventFlag(L3_event_timerExpired);
}

// 사용자별 타이머 시작
void L3_timer_startUserTimer(uint8_t* userId, int timerIndex)
{
    uint8_t experienceTime = DEFAULT_EXPERIENCE_TIME;

    memcpy(timerUserIds[timerIndex], userId, ID_SIZE);
    userTimers[timerIndex].attach(L3_timer_timeoutHandler,
                                  (void*)timerUserIds[timerIndex],
                                  experienceTime);
    timerStatus[timerIndex] = 1;
}