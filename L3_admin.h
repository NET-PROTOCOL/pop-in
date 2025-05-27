#ifndef L3_ADMIN_H
#define L3_ADMIN_H

#include "mbed.h"

// Admin mode status
#define ADMIN_MODE_INACTIVE     0
#define ADMIN_MODE_ACTIVE       1

// Broadcast message types
#define L3_MSG_TYPE_ANNOUNCEMENT    0x30

// User status
#define USER_STATUS_CONNECTED       0
#define USER_STATUS_WAITING         1

// Maximum limits
#define MAX_CONNECTED_USERS         20
#define MAX_WAITING_USERS          10
#define MAX_ANNOUNCEMENT_SIZE      100

// User info structure
typedef struct {
    uint8_t userId;
    uint8_t status;         // connected or waiting
    int16_t rssi;
    int8_t snr;
    uint32_t connectTime;   // connection timestamp
    uint8_t isActive;
} UserInfo_t;

// Booth info structure
typedef struct {
    uint8_t boothId;
    uint8_t capacity;
    uint8_t currentUsers;
    uint8_t waitingUsers;
    uint8_t isOperational;
} BoothInfo_t;

// Function declarations
void L3_admin_init(uint8_t boothId, uint8_t capacity);
void L3_admin_activate(void);
void L3_admin_deactivate(void);
uint8_t L3_admin_getStatus(void);

// User management functions
void L3_admin_addUser(uint8_t userId, int16_t rssi, int8_t snr);
void L3_admin_removeUser(uint8_t userId);
void L3_admin_moveUserToWaiting(uint8_t userId);
void L3_admin_moveWaitingToConnected(uint8_t userId);

// Command processing functions
void L3_admin_processCommand(char* command);
void L3_admin_sendBroadcast(char* message);
void L3_admin_showBoothInfo(void);
void L3_admin_showUserList(void);
void L3_admin_showWaitingQueue(void);

// Input processing
void L3_admin_processInput(char c);
uint8_t L3_admin_isCommandReady(void);
char* L3_admin_getCommand(void);

// Utility functions
uint8_t L3_admin_getUserCount(void);
uint8_t L3_admin_getWaitingCount(void);
BoothInfo_t* L3_admin_getBoothInfo(void);

#endif // L3_ADMIN_H