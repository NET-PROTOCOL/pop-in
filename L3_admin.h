#ifndef L3_ADMIN_H
#define L3_ADMIN_H

#include "mbed.h"

// Admin mode status
#define ADMIN_MODE_INACTIVE     0
#define ADMIN_MODE_ACTIVE       1

// Broadcast message types
#define L3_MSG_TYPE_ANNOUNCEMENT    0x30

// User status - FSM 상태와 연동
#define USER_STATUS_SCANNING        0
#define USER_STATUS_CONNECTED       1
#define USER_STATUS_IN_USE          2  // Registered list에 포함
#define USER_STATUS_WAITING         3  // Waiting queue에 포함

// Maximum limits
#define MAX_CONNECTED_USERS         20
#define MAX_WAITING_USERS          10
#define MAX_REGISTERED_USERS       50  // 등록된 사용자는 더 많이 저장
#define MAX_ANNOUNCEMENT_SIZE      100

// User info structure - 확장된 정보
typedef struct {
    uint8_t userId;
    uint8_t status;         // FSM 상태와 연동
    int16_t rssi;
    int8_t snr;
    uint32_t connectTime;   // 연결 시작 시간
    uint32_t useStartTime;  // 사용 시작 시간 (IN_USE 전이 시점)
    uint32_t totalUseTime;  // 누적 사용 시간
    uint8_t waitingNumber;  // 대기번호 (WAITING 상태일 때)
    uint8_t isActive;       // 현재 활성 상태
    uint8_t isRegistered;   // 등록 여부 (한번이라도 IN_USE 된 경우)
} UserInfo_t;

// Booth info structure - 확장된 정보
typedef struct {
    uint8_t boothId;
    uint8_t capacity;           // 동시 사용 가능 인원
    uint8_t currentUsers;       // 현재 연결된 사용자 (CONNECTED)
    uint8_t activeUsers;        // 현재 사용중인 사용자 (IN_USE)
    uint8_t waitingUsers;       // 대기중인 사용자 (WAITING)
    uint8_t registeredUsers;    // 등록된 사용자 총 수
    uint8_t nextWaitingNumber;  // 다음 대기번호
    uint8_t isOperational;
} BoothInfo_t;

// Function declarations
void L3_admin_init(uint8_t boothId, uint8_t capacity);
void L3_admin_activate(void);
void L3_admin_deactivate(void);
uint8_t L3_admin_getStatus(void);

// User management functions - FSM 상태 기반
void L3_admin_addUser(uint8_t userId, int16_t rssi, int8_t snr);
void L3_admin_removeUser(uint8_t userId);
void L3_admin_updateUserStatus(uint8_t userId, uint8_t newStatus);
void L3_admin_moveToInUse(uint8_t userId);          // CONNECTED -> IN_USE
void L3_admin_moveToWaiting(uint8_t userId);        // CONNECTED -> WAITING
void L3_admin_moveWaitingToConnected(uint8_t userId); // WAITING -> CONNECTED
void L3_admin_exitFromInUse(uint8_t userId);        // IN_USE -> CONNECTED

// Command processing functions - 확장된 명령어
void L3_admin_processCommand(char* command);
void L3_admin_sendBroadcast(char* message);
void L3_admin_showBoothInfo(void);
void L3_admin_showConnectedUsers(void);     // 현재 연결된 사용자
void L3_admin_showActiveUsers(void);        // 현재 사용중인 사용자
void L3_admin_showWaitingQueue(void);       // 대기 큐
void L3_admin_showRegisteredList(void);     // 등록된 사용자 목록
void L3_admin_showUserStatistics(void);     // 사용자 통계

// Input processing
void L3_admin_processInput(char c);
uint8_t L3_admin_isCommandReady(void);
char* L3_admin_getCommand(void);

// Utility functions
uint8_t L3_admin_getUserCount(void);
uint8_t L3_admin_getActiveCount(void);      // 사용중인 사용자 수
uint8_t L3_admin_getWaitingCount(void);
uint8_t L3_admin_getRegisteredCount(void);  // 등록된 사용자 수
uint8_t L3_admin_canAcceptUser(void);       // 새 사용자 수용 가능 여부
BoothInfo_t* L3_admin_getBoothInfo(void);
UserInfo_t* L3_admin_findUser(uint8_t userId);

#endif // L3_ADMIN_H