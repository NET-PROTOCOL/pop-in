#include "L3_admin.h"
#include "L3_LLinterface.h"
#include "L3_FSMevent.h"
#include "protocol_parameters.h"
#include "mbed.h"
#include <string.h>

// Global variables
static uint8_t adminModeStatus = ADMIN_MODE_INACTIVE;
static BoothInfo_t boothInfo;
static UserInfo_t connectedUsers[MAX_CONNECTED_USERS];
static UserInfo_t waitingUsers[MAX_WAITING_USERS];

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
    boothInfo.waitingUsers = 0;
    boothInfo.isOperational = 1;
    
    // Initialize user arrays
    for (int i = 0; i < MAX_CONNECTED_USERS; i++) {
        connectedUsers[i].isActive = 0;
    }
    
    for (int i = 0; i < MAX_WAITING_USERS; i++) {
        waitingUsers[i].isActive = 0;
    }
    
    // Reset command buffer
    commandLength = 0;
    commandReady = 0;
    
    pc.printf("[BOOTH] Booth manager initialized\n");
}

void L3_admin_activate(void)
{
    adminModeStatus = ADMIN_MODE_ACTIVE;
    pc.printf("[ADMIN] Admin mode activated - Booth operation enabled\n");
    pc.printf("Available booth commands:\n");
    pc.printf("  - 'b message': Send broadcast announcement\n");
    pc.printf("  - 'i': Check booth information\n");
    pc.printf("  - 'u': Check active user list\n");
    pc.printf("  - 'w': Check waiting queue\n");
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

// User management functions
void L3_admin_addUser(uint8_t userId, int16_t rssi, int8_t snr)
{
    // Try to add to connected users first
    if (boothInfo.currentUsers < boothInfo.capacity) {
        for (int i = 0; i < MAX_CONNECTED_USERS; i++) {
            if (!connectedUsers[i].isActive) {
                connectedUsers[i].userId = userId;
                connectedUsers[i].status = USER_STATUS_CONNECTED;
                connectedUsers[i].rssi = rssi;
                connectedUsers[i].snr = snr;
                connectedUsers[i].connectTime = time(NULL);
                connectedUsers[i].isActive = 1;
                boothInfo.currentUsers++;
                
                pc.printf("[BOOTH] User %d connected (RSSI: %d, SNR: %d)\n", userId, rssi, snr);
                pc.printf("Board connected : %d\n", boothInfo.currentUsers);
                return;
            }
        }
    } else {
        // Add to waiting queue
        for (int i = 0; i < MAX_WAITING_USERS; i++) {
            if (!waitingUsers[i].isActive) {
                waitingUsers[i].userId = userId;
                waitingUsers[i].status = USER_STATUS_WAITING;
                waitingUsers[i].rssi = rssi;
                waitingUsers[i].snr = snr;
                waitingUsers[i].connectTime = time(NULL);
                waitingUsers[i].isActive = 1;
                boothInfo.waitingUsers++;
                
                pc.printf("[BOOTH] User %d added to waiting queue (RSSI: %d, SNR: %d)\n", userId, rssi, snr);
                return;
            }
        }
        pc.printf("[BOOTH] Cannot add user %d - booth and waiting queue full\n", userId);
    }
}

void L3_admin_removeUser(uint8_t userId)
{
    // Remove from connected users
    for (int i = 0; i < MAX_CONNECTED_USERS; i++) {
        if (connectedUsers[i].isActive && connectedUsers[i].userId == userId) {
            connectedUsers[i].isActive = 0;
            boothInfo.currentUsers--;
            pc.printf("[BOOTH] User %d disconnected\n", userId);
            pc.printf("Board connected : %d\n", boothInfo.currentUsers);
            
            // Try to move someone from waiting queue
            if (boothInfo.waitingUsers > 0) {
                for (int j = 0; j < MAX_WAITING_USERS; j++) {
                    if (waitingUsers[j].isActive) {
                        L3_admin_moveWaitingToConnected(waitingUsers[j].userId);
                        break;
                    }
                }
            }
            return;
        }
    }
    
    // Remove from waiting queue
    for (int i = 0; i < MAX_WAITING_USERS; i++) {
        if (waitingUsers[i].isActive && waitingUsers[i].userId == userId) {
            waitingUsers[i].isActive = 0;
            boothInfo.waitingUsers--;
            pc.printf("[BOOTH] User %d removed from waiting queue\n", userId);
            return;
        }
    }
}

void L3_admin_moveWaitingToConnected(uint8_t userId)
{
    // Find user in waiting queue
    for (int i = 0; i < MAX_WAITING_USERS; i++) {
        if (waitingUsers[i].isActive && waitingUsers[i].userId == userId) {
            // Move to connected users
            for (int j = 0; j < MAX_CONNECTED_USERS; j++) {
                if (!connectedUsers[j].isActive) {
                    connectedUsers[j] = waitingUsers[i];
                    connectedUsers[j].status = USER_STATUS_CONNECTED;
                    connectedUsers[j].connectTime = time(NULL);
                    
                    // Remove from waiting queue
                    waitingUsers[i].isActive = 0;
                    
                    boothInfo.currentUsers++;
                    boothInfo.waitingUsers--;
                    
                    pc.printf("[BOOTH] User %d moved from waiting to connected\n", userId);
                    pc.printf("Board connected : %d\n", boothInfo.currentUsers);
                    return;
                }
            }
            break;
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
    } else if (command[0] == 'u' && command[1] == '\0') {
        // Show user list
        L3_admin_showUserList();
    } else if (command[0] == 'w' && command[1] == '\0') {
        // Show waiting queue
        L3_admin_showWaitingQueue();
    } else {
        pc.printf("[ADMIN] Unknown command. Available commands: b, i, u, w\n");
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
    pc.printf("Capacity: %d\n", boothInfo.capacity);
    pc.printf("Connected Users: %d\n", boothInfo.currentUsers);
    pc.printf("Waiting Users: %d\n", boothInfo.waitingUsers);
    pc.printf("Operational: %s\n", boothInfo.isOperational ? "Yes" : "No");
    pc.printf("========================\n");
}

void L3_admin_showUserList(void)
{
    pc.printf("\n=== CONNECTED USERS ===\n");
    if (boothInfo.currentUsers == 0) {
        pc.printf("No users connected.\n");
    } else {
        pc.printf("ID  | RSSI | SNR | Connect Time\n");
        pc.printf("----+------+-----+-------------\n");
        for (int i = 0; i < MAX_CONNECTED_USERS; i++) {
            if (connectedUsers[i].isActive) {
                pc.printf("%-3d | %-4d | %-3d | %lu\n", 
                         connectedUsers[i].userId,
                         connectedUsers[i].rssi,
                         connectedUsers[i].snr,
                         connectedUsers[i].connectTime);
            }
        }
    }
    pc.printf("======================\n");
}

void L3_admin_showWaitingQueue(void)
{
    pc.printf("\n=== WAITING QUEUE ===\n");
    if (boothInfo.waitingUsers == 0) {
        pc.printf("No users waiting.\n");
    } else {
        pc.printf("ID  | RSSI | SNR | Wait Time\n");
        pc.printf("----+------+-----+----------\n");
        for (int i = 0; i < MAX_WAITING_USERS; i++) {
            if (waitingUsers[i].isActive) {
                pc.printf("%-3d | %-4d | %-3d | %lu\n", 
                         waitingUsers[i].userId,
                         waitingUsers[i].rssi,
                         waitingUsers[i].snr,
                         waitingUsers[i].connectTime);
            }
        }
    }
    pc.printf("====================\n");
}

// Utility functions
uint8_t L3_admin_getUserCount(void)
{
    return boothInfo.currentUsers;
}

uint8_t L3_admin_getWaitingCount(void)
{
    return boothInfo.waitingUsers;
}

BoothInfo_t* L3_admin_getBoothInfo(void)
{
    return &boothInfo;
}