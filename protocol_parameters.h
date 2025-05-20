#define DBGMSG_L2                       0 //debug print control
#define DBGMSG_L3                       0 //debug print control

#define L3_MAXDATASIZE                  1024


#define L2_ARQ_MAXRETRANSMISSION        10
#define L2_ARQ_MAXWAITTIME              5
#define L2_ARQ_MINWAITTIME              2

// 사용자 및 부스 관련 파라미터
#define USER_ID_SIZE                   8    // 사용자 ID 크기
#define MAX_BASE_STATIONS              10   // 최대 감지 가능한 기지국 수
#define DEFAULT_EXPERIENCE_TIME        300  // 기본 체험 시간 (초)
#define MAX_CAPACITY                   20   // 부스 최대 수용 인원
#define MAX_WAITING                    50   // 최대 대기 인원
#define PRESENCE_CHECK_INTERVAL        10   // 근접 감지 주기 (초)
