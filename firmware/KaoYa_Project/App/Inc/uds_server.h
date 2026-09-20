#ifndef UDS_SERVER_H
#define UDS_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#define UDS_CAN_ID_PHYSICAL_REQUEST   (0x7E0u)
#define UDS_CAN_ID_PHYSICAL_RESPONSE  (0x7E8u)

#define UDS_SESSION_DEFAULT           (0x01u)
#define UDS_SESSION_PROGRAMMING       (0x02u)
#define UDS_SESSION_EXTENDED          (0x03u)

void UdsServer_Init(void);
void UdsServer_ProcessCanFrame(const uint8_t data[8], uint8_t dlc,
                               uint32_t now_ms);
bool UdsServer_PeekResponse(uint8_t data[8]);
void UdsServer_ConfirmResponseSent(uint32_t now_ms);
void UdsServer_Service(uint32_t now_ms);
uint8_t UdsServer_GetSession(void);

#endif /* UDS_SERVER_H */
