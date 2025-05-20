#include "mbed.h"
#include "L2_FSMevent.h"
#include "protocol_parameters.h"



//ARQ retransmission timer
static Timeout timer;                       
static uint8_t timerStatus = 0;


//timer event : ARQ timeout
void L2_timer_timeoutHandler(void) 
{
    timerStatus = 0;
    L2_event_setEventFlag(L2_event_arqTimeout);
}

//timer related functions ---------------------------
void L2_timer_startTimer()
{
    uint8_t waitTime = L2_ARQ_MINWAITTIME + rand()%(L2_ARQ_MAXWAITTIME-L2_ARQ_MINWAITTIME);
    timer.attach(L2_timer_timeoutHandler, waitTime);
    timerStatus = 1;
}

void L2_timer_stopTimer()
{
    timer.detach();
    timerStatus = 0;
}

uint8_t L2_timer_getTimerStatus()
{
    return timerStatus;
}

// 근접 감지를 위한 주기적 타이머
static Ticker presenceCheckTicker;

void L2_timer_presenceCheckHandler(void)
{
    // 주변 기지국 감지 이벤트 생성
    if (presenceDetectionActive) {
        phymac_requestScan();
    }
}

void L2_timer_startPresenceCheck(void)
{
    presenceCheckTicker.attach(L2_timer_presenceCheckHandler, PRESENCE_CHECK_INTERVAL);
}

void L2_timer_stopPresenceCheck(void)
{
    presenceCheckTicker.detach();
}
