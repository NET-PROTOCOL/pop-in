extern void (*L3_LLI_dataReqFunc)(uint8_t* msg, uint8_t size, uint8_t destId);

// Data indication and confirmation functions
void L3_LLI_dataInd(uint8_t* dataPtr, uint8_t srcId, uint8_t size, int8_t snr, int16_t rssi);
void L3_LLI_dataCnf(uint8_t res);
void L3_LLI_reconfigSrcIdCnf(uint8_t res);

// Getter functions for received message info
uint8_t* L3_LLI_getMsgPtr();
uint8_t L3_LLI_getSize();
uint8_t L3_LLI_getSrcId();

// New functions to get RSSI and SNR information
int16_t L3_LLI_getRssi();
int8_t L3_LLI_getSnr();
int16_t L3_LLI_getCurrentRssi();
int8_t L3_LLI_getCurrentSnr();

// Setter functions for callback registration
void L3_LLI_setDataReqFunc(void (*funcPtr)(uint8_t*, uint8_t, uint8_t));
void L3_LLI_setReconfigSrcIdReqFunc(void (*funcPtr)(uint8_t));