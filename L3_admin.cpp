#include "L3_admin.h"
#include "L3_LLinterface.h"
#include "L3_FSMevent.h"
#include "protocol_parameters.h"
#include "mbed.h"
#include <string.h>

// Global variables
static uint8_t adminModeStatus = ADMIN_MODE_INACTIVE;
static BoothInfo_t boothInfo;
static UserInfo_t connectedUsers[MAX_CONNECTED_USERS];     // 현재 연결된 사용자
static UserInfo_t waitingUsers[MAX_WAITING_USERS];         // 대기 큐
static UserInfo_t registeredUsers[MAX_REGISTERED_USERS];   // 등록된 사용자 (영구 저장)

// Command input buffer
static char commandBuffer[MAX_ANNOUNCEMENT_SIZE];
static uint8_t commandLength = 0;
static uint8_t commandReady = 0;

// Serial interface
extern Serial pc;

// Announcement message structure
typedef struct {
    uint8_t msgType;
    uint8_t boothId;
    uint8_t announcementLength;
    char message[MAX_ANNOUNCEMENT_SIZE];
} AnnouncementMsg_t;

// Admin initialization
void L3_admin_init(uint8_t boothId, uint8_t capacity)
{
    // Initialize booth info
    boothInfo.boothId = boothId;
    boothInfo.capacity = capacity;
    boothInfo.currentUsers = 0;
    boothInfo.activeUsers = 0;
    boothInfo.waitingUsers = 0;
    boothInfo.registeredUsers = 0;
    boothInfo.nextWaitingNumber = 1;
    boothInfo.isOperational = 1;
    
    // Initialize user arrays
    for (int i = 0; i < MAX_CONNECTED_USERS; i++) {
        connectedUsers[i].isActive = 0;
        connectedUsers[i].isRegistered = 0;
    }
    
    for (int i = 0; i < MAX_WAITING_USERS; i++) {
        waitingUsers[i].isActive = 0;
        waitingUsers[i].isRegistered = 0;
    }
    
    for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
        registeredUsers[i].isActive = 0;
        registeredUsers[i].isRegistered = 0;
    }
    
    // Reset command buffer
    commandLength = 0;
    commandReady = 0;
    
    pc.printf("[BOOTH] Booth manager initialized (ID: %d, Capacity: %d)\n", boothId, capacity);
}

void L3_admin_activate(void)
{
    adminModeStatus = ADMIN_MODE_ACTIVE;
    pc.printf("[ADMIN] Admin mode activated - Enhanced booth operation enabled\n");
    pc.printf("Available booth commands:\n");
    pc.printf("  - 'b message': Send broadcast announcement\n");
    pc.printf("  - 'i': Check booth information\n");
    pc.printf("  - 'c': Check connected users\n");
    pc.printf("  - 'a': Check active users (IN_USE)\n");
    pc.printf("  - 'w': Check waiting queue\n");
    pc.printf("  - 'r': Check registered list\n");
    pc.printf("  - 's': Show user statistics\n");
}

void L3_admin_deactivate(void)
{
    adminModeStatus = ADMIN_MODE_INACTIVE;
    pc.printf("[ADMIN] Admin mode deactivated\n");
}

uint8_t L3_admin_getStatus(void)
{
    return adminModeStatus;
}

// 사용자 찾기 함수
UserInfo_t* L3_admin_findUser(uint8_t userId)
{
    // Connected users에서 찾기
    for (int i = 0; i < MAX_CONNECTED_USERS; i++) {
        if (connectedUsers[i].isActive && connectedUsers[i].userId == userId) {
            return &connectedUsers[i];
        }
    }
    
    // Waiting users에서 찾기
    for (int i = 0; i < MAX_WAITING_USERS; i++) {
        if (waitingUsers[i].isActive && waitingUsers[i].userId == userId) {
            return &waitingUsers[i];
        }
    }
    
    return NULL;
}

// User management functions
void L3_admin_addUser(uint8_t userId, int16_t rssi, int8_t snr)
{
    // 이미 연결된 사용자인지 확인
    if (L3_admin_findUser(userId) != NULL) {
        pc.printf("[BOOTH] User %d already exists\n", userId);
        return;
    }
    
    // Connected users에 추가 (SCANNING -> CONNECTED 상태)
    for (int i = 0; i < MAX_CONNECTED_USERS; i++) {
        if (!connectedUsers[i].isActive) {
            connectedUsers[i].userId = userId;
            connectedUsers[i].status = USER_STATUS_CONNECTED;
            connectedUsers[i].rssi = rssi;
            connectedUsers[i].snr = snr;
            connectedUsers[i].connectTime = time(NULL);
            connectedUsers[i].useStartTime = 0;
            connectedUsers[i].totalUseTime = 0;
            connectedUsers[i].waitingNumber = 0;
            connectedUsers[i].isActive = 1;
            connectedUsers[i].isRegistered = 0;
            boothInfo.currentUsers++;
            
            pc.printf("[BOOTH] User %d connected (RSSI: %d, SNR: %d)\n", userId, rssi, snr);
            pc.printf("Connected users: %d/%d\n", boothInfo.currentUsers, MAX_CONNECTED_USERS);
            return;
        }
    }
    
    pc.printf("[BOOTH] Cannot add user %d - connection slots full\n", userId);
}

void L3_admin_removeUser(uint8_t userId)
{
    // Connected users에서 제거
    for (int i = 0; i < MAX_CONNECTED_USERS; i++) {
        if (connectedUsers[i].isActive && connectedUsers[i].userId == userId) {
            // IN_USE 상태였다면 active count 감소
            if (connectedUsers[i].status == USER_STATUS_IN_USE) {
                boothInfo.activeUsers--;
            }
            
            connectedUsers[i].isActive = 0;
            boothInfo.currentUsers--;
            pc.printf("[BOOTH] User %d disconnected\n", userId);
            pc.printf("Connected users: %d\n", boothInfo.currentUsers);
            return;
        }
    }
    
    // Waiting users에서 제거
    for (int i = 0; i < MAX_WAITING_USERS; i++) {
        if (waitingUsers[i].isActive && waitingUsers[i].userId == userId) {
            waitingUsers[i].isActive = 0;
            boothInfo.waitingUsers--;
            pc.printf("[BOOTH] User %d removed from waiting queue\n", userId);
            return;
        }
    }
}

// FSM 상태 업데이트
void L3_admin_updateUserStatus(uint8_t userId, uint8_t newStatus)
{
    UserInfo_t* user = L3_admin_findUser(userId);
    if (user == NULL) {
        pc.printf("[BOOTH] User %d not found for status update\n", userId);
        return;
    }
    
    uint8_t oldStatus = user->status;
    user->status = newStatus;
    
    pc.printf("[BOOTH] User %d status changed: %d -> %d\n", userId, oldStatus, newStatus);
    
    // 상태별 처리
    switch (newStatus) {
        case USER_STATUS_IN_USE:
            L3_admin_moveToInUse(userId);
            break;
        case USER_STATUS_WAITING:
            L3_admin_moveToWaiting(userId);
            break;
    }
}

// CONNECTED -> IN_USE 전이
void L3_admin_moveToInUse(uint8_t userId)
{
    // capacity 체크
    if (boothInfo.activeUsers >= boothInfo.capacity) {
        pc.printf("[BOOTH] Cannot move user %d to IN_USE - capacity full (%d/%d)\n", 
                 userId, boothInfo.activeUsers, boothInfo.capacity);
        // 자동으로 WAITING으로 전이
        L3_admin_moveToWaiting(userId);
        return;
    }
    
    UserInfo_t* user = L3_admin_findUser(userId);
    if (user && user->status == USER_STATUS_CONNECTED) {
        user->status = USER_STATUS_IN_USE;
        user->useStartTime = time(NULL);
        boothInfo.activeUsers++;
        
        // Registered list에 추가 (한번도 등록되지 않은 경우)
        if (!user->isRegistered) {
            for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
                if (!registeredUsers[i].isActive) {
                    registeredUsers[i] = *user;
                    registeredUsers[i].isRegistered = 1;
                    registeredUsers[i].isActive = 1;
                    user->isRegistered = 1;
                    boothInfo.registeredUsers++;
                    break;
                }
            }
        }
        
        pc.printf("[BOOTH] User %d moved to IN_USE (%d/%d active)\n", 
                 userId, boothInfo.activeUsers, boothInfo.capacity);
    }
}

// CONNECTED -> WAITING 전이
void L3_admin_moveToWaiting(uint8_t userId)
{
    // Connected users에서 찾기
    for (int i = 0; i < MAX_CONNECTED_USERS; i++) {
        if (connectedUsers[i].isActive && connectedUsers[i].userId == userId) {
            // 대기 큐에 공간이 있는지 확인
            for (int j = 0; j < MAX_WAITING_USERS; j++) {
                if (!waitingUsers[j].isActive) {
                    // 대기 큐로 이동
                    waitingUsers[j] = connectedUsers[i];
                    waitingUsers[j].status = USER_STATUS_WAITING;
                    waitingUsers[j].waitingNumber = boothInfo.nextWaitingNumber++;
                    
                    // Connected users에서 제거
                    connectedUsers[i].isActive = 0;
                    boothInfo.currentUsers--;
                    boothInfo.waitingUsers++;
                    
                    pc.printf("[BOOTH] User %d moved to waiting queue (Number: %d)\n", 
                             userId, waitingUsers[j].waitingNumber);
                    return;
                }
            }
            pc.printf("[BOOTH] Cannot move user %d to waiting - queue full\n", userId);
            return;
        }
    }
}

// WAITING -> CONNECTED 전이
void L3_admin_moveWaitingToConnected(uint8_t userId)
{
    // Waiting users에서 찾기
    for (int i = 0; i < MAX_WAITING_USERS; i++) {
        if (waitingUsers[i].isActive && waitingUsers[i].userId == userId) {
            // Connected users에 공간이 있는지 확인
            for (int j = 0; j < MAX_CONNECTED_USERS; j++) {
                if (!connectedUsers[j].isActive) {
                    // Connected로 이동
                    connectedUsers[j] = waitingUsers[i];
                    connectedUsers[j].status = USER_STATUS_CONNECTED;
                    connectedUsers[j].waitingNumber = 0;
                    
                    // Waiting queue에서 제거
                    waitingUsers[i].isActive = 0;
                    boothInfo.waitingUsers--;
                    boothInfo.currentUsers++;
                    
                    pc.printf("[BOOTH] User %d moved from waiting to connected\n", userId);
                    return;
                }
            }
            pc.printf("[BOOTH] Cannot move user %d from waiting - no connection slots\n", userId);
            return;
        }
    }
}

// IN_USE -> CONNECTED 전이
void L3_admin_exitFromInUse(uint8_t userId)
{
    UserInfo_t* user = L3_admin_findUser(userId);
    if (user && user->status == USER_STATUS_IN_USE) {
        // 사용 시간 업데이트
        uint32_t currentTime = time(NULL);
        if (user->useStartTime > 0) {
            user->totalUseTime += (currentTime - user->useStartTime);
            
            // Registered list 업데이트
            for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
                if (registeredUsers[i].isActive && registeredUsers[i].userId == userId) {
                    registeredUsers[i].totalUseTime = user->totalUseTime;
                    break;
                }
            }
        }
        
        user->status = USER_STATUS_CONNECTED;
        user->useStartTime = 0;
        boothInfo.activeUsers--;
        
        pc.printf("[BOOTH] User %d exited from IN_USE (%d/%d active)\n", 
                 userId, boothInfo.activeUsers, boothInfo.capacity);
        
        // 대기중인 사용자가 있다면 자동으로 연결
        if (boothInfo.waitingUsers > 0) {
            for (int i = 0; i < MAX_WAITING_USERS; i++) {
                if (waitingUsers[i].isActive) {
                    L3_admin_moveWaitingToConnected(waitingUsers[i].userId);
                    break;
                }
            }
        }
    }
}

// Command processing functions
void L3_admin_processInput(char c)
{
    if (adminModeStatus != ADMIN_MODE_ACTIVE) {
        return;
    }
    
    if (c == '\n' || c == '\r') {
        if (commandLength > 0) {
            commandBuffer[commandLength] = '\0';
            commandReady = 1;
        }
    } else if (c == '\b' || c == 127) { // Backspace
        if (commandLength > 0) {
            commandLength--;
            pc.printf("\b \b"); // Erase character from terminal
        }
    } else if (commandLength < MAX_ANNOUNCEMENT_SIZE - 1) {
        commandBuffer[commandLength++] = c;
        pc.printf("%c", c); // Echo character
    }
}

uint8_t L3_admin_isCommandReady(void)
{
    return commandReady;
}

char* L3_admin_getCommand(void)
{
    commandReady = 0;
    commandLength = 0;
    return commandBuffer;
}

void L3_admin_processCommand(char* command)
{
    if (command[0] == 'b' && command[1] == ' ') {
        // Broadcast announcement
        L3_admin_sendBroadcast(command + 2);
    } else if (command[0] == 'i' && command[1] == '\0') {
        // Show booth information
        L3_admin_showBoothInfo();
    } else if (command[0] == 'c' && command[1] == '\0') {
        // Show connected users
        L3_admin_showConnectedUsers();
    } else if (command[0] == 'a' && command[1] == '\0') {
        // Show active users (IN_USE)
        L3_admin_showActiveUsers();
    } else if (command[0] == 'w' && command[1] == '\0') {
        // Show waiting queue
        L3_admin_showWaitingQueue();
    } else if (command[0] == 'r' && command[1] == '\0') {
        // Show registered list
        L3_admin_showRegisteredList();
    } else if (command[0] == 's' && command[1] == '\0') {
        // Show statistics
        L3_admin_showUserStatistics();
    } else {
        pc.printf("[ADMIN] Unknown command. Available: b, i, c, a, w, r, s\n");
    }
}

void L3_admin_sendBroadcast(char* message)
{
    AnnouncementMsg_t announcement;
    announcement.msgType = L3_MSG_TYPE_ANNOUNCEMENT;
    announcement.boothId = boothInfo.boothId;
    announcement.announcementLength = strlen(message);
    
    if (announcement.announcementLength >= MAX_ANNOUNCEMENT_SIZE) {
        announcement.announcementLength = MAX_ANNOUNCEMENT_SIZE - 1;
    }
    
    strncpy(announcement.message, message, announcement.announcementLength);
    announcement.message[announcement.announcementLength] = '\0';
    
    // Send broadcast to all nodes (ID 255 = broadcast)
    L3_LLI_dataReqFunc((uint8_t*)&announcement, 
                       sizeof(uint8_t) * 3 + announcement.announcementLength + 1, 
                       255);
    
    pc.printf("[ADMIN] Broadcast sent: %s\n", message);
}

void L3_admin_showBoothInfo(void)
{
    pc.printf("\n=== BOOTH INFORMATION ===\n");
    pc.printf("Booth ID: %d\n", boothInfo.boothId);
    pc.printf("Capacity: %d (simultaneous users)\n", boothInfo.capacity);
    pc.printf("Connected Users: %d\n", boothInfo.currentUsers);
    pc.printf("Active Users (IN_USE): %d/%d\n", boothInfo.activeUsers, boothInfo.capacity);
    pc.printf("Waiting Users: %d\n", boothInfo.waitingUsers);
    pc.printf("Total Registered: %d\n", boothInfo.registeredUsers);
    pc.printf("Next Waiting Number: %d\n", boothInfo.nextWaitingNumber);
    pc.printf("Operational: %s\n", boothInfo.isOperational ? "Yes" : "No");
    pc.printf("========================\n");
}

void L3_admin_showConnectedUsers(void)
{
    pc.printf("\n=== CONNECTED USERS ===\n");
    if (boothInfo.currentUsers == 0) {
        pc.printf("No users connected.\n");
    } else {
        pc.printf("ID  | Status | RSSI | SNR | Connect Time | Registered\n");
        pc.printf("----+--------+------+-----+--------------+-----------\n");
        for (int i = 0; i < MAX_CONNECTED_USERS; i++) {
            if (connectedUsers[i].isActive) {
                const char* statusStr = (connectedUsers[i].status == USER_STATUS_CONNECTED) ? "CONN" :
                                      (connectedUsers[i].status == USER_STATUS_IN_USE) ? "ACTIVE" : "OTHER";
                pc.printf("%-3d | %-6s | %-4d | %-3d | %-12lu | %s\n", 
                         connectedUsers[i].userId,
                         statusStr,
                         connectedUsers[i].rssi,
                         connectedUsers[i].snr,
                         connectedUsers[i].connectTime,
                         connectedUsers[i].isRegistered ? "Yes" : "No");
            }
        }
    }
    pc.printf("=======================\n");
}

void L3_admin_showActiveUsers(void)
{
    pc.printf("\n=== ACTIVE USERS (IN_USE) ===\n");
    if (boothInfo.activeUsers == 0) {
        pc.printf("No users currently active.\n");
    } else {
        pc.printf("ID  | RSSI | SNR | Use Start Time | Duration\n");
        pc.printf("----+------+-----+----------------+---------\n");
        uint32_t currentTime = time(NULL);
        for (int i = 0; i < MAX_CONNECTED_USERS; i++) {
            if (connectedUsers[i].isActive && connectedUsers[i].status == USER_STATUS_IN_USE) {
                uint32_t duration = (connectedUsers[i].useStartTime > 0) ? 
                                   (currentTime - connectedUsers[i].useStartTime) : 0;
                pc.printf("%-3d | %-4d | %-3d | %-14lu | %lu sec\n", 
                         connectedUsers[i].userId,
                         connectedUsers[i].rssi,
                         connectedUsers[i].snr,
                         connectedUsers[i].useStartTime,
                         duration);
            }
        }
    }
    pc.printf("=============================\n");
}

void L3_admin_showWaitingQueue(void)
{
    pc.printf("\n=== WAITING QUEUE ===\n");
    if (boothInfo.waitingUsers == 0) {
        pc.printf("No users waiting.\n");
    } else {
        pc.printf("Num | ID  | RSSI | SNR | Wait Start Time\n");
        pc.printf("----+-----+------+-----+----------------\n");
        for (int i = 0; i < MAX_WAITING_USERS; i++) {
            if (waitingUsers[i].isActive) {
                pc.printf("%-3d | %-3d | %-4d | %-3d | %lu\n", 
                         waitingUsers[i].waitingNumber,
                         waitingUsers[i].userId,
                         waitingUsers[i].rssi,
                         waitingUsers[i].snr,
                         waitingUsers[i].connectTime);
            }
        }
    }
    pc.printf("=====================\n");
}

void L3_admin_showRegisteredList(void)
{
    pc.printf("\n=== REGISTERED USERS ===\n");
    if (boothInfo.registeredUsers == 0) {
        pc.printf("No registered users.\n");
    } else {
        pc.printf("ID  | RSSI | SNR | First Use Time | Total Use Time\n");
        pc.printf("----+------+-----+----------------+---------------\n");
        for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
            if (registeredUsers[i].isActive) {
                pc.printf("%-3d | %-4d | %-3d | %-14lu | %lu sec\n", 
                         registeredUsers[i].userId,
                         registeredUsers[i].rssi,
                         registeredUsers[i].snr,
                         registeredUsers[i].useStartTime,
                         registeredUsers[i].totalUseTime);
            }
        }
    }
    pc.printf("========================\n");
}

void L3_admin_showUserStatistics(void)
{
    uint32_t totalUseTime = 0;
    uint32_t avgUseTime = 0;
    
    for (int i = 0; i < MAX_REGISTERED_USERS; i++) {
        if (registeredUsers[i].isActive) {
            totalUseTime += registeredUsers[i].totalUseTime;
        }
    }
    
    if (boothInfo.registeredUsers > 0) {
        avgUseTime = totalUseTime / boothInfo.registeredUsers;
    }
    
    pc.printf("\n=== USER STATISTICS ===\n");
    pc.printf("Total Registered Users: %d\n", boothInfo.registeredUsers);
    pc.printf("Currently Connected: %d\n", boothInfo.currentUsers);
    pc.printf("Currently Active: %d/%d\n", boothInfo.activeUsers, boothInfo.capacity);
    pc.printf("Currently Waiting: %d\n", boothInfo.waitingUsers);
    pc.printf("Total Usage Time: %lu seconds\n", totalUseTime);
    pc.printf("Average Usage Time: %lu seconds\n", avgUseTime);
    pc.printf("Booth Utilization: %.1f%%\n", 
             boothInfo.capacity > 0 ? (float)boothInfo.activeUsers * 100.0f / boothInfo.capacity : 0.0f);
    pc.printf("=======================\n");
}