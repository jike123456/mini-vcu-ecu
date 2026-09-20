#include "boot_diag.h"

#include <stdint.h>
#include <string.h>

#include "boot_image.h"
#include "boot_layout.h"
#include "stm32f4xx.h"

#define UART_CLOCK_HZ              (16000000UL)
#define UART_BAUD_RATE             (115200UL)

#define OUTER_SOF0                 (0xAAu)
#define OUTER_SOF1                 (0x55u)
#define OUTER_EOF0                 (0x66u)
#define OUTER_EOF1                 (0xBBu)
#define OUTER_VERSION              (0x01u)
#define OUTER_MSG_VCAN_RX          (0x02u)
#define OUTER_MSG_VCAN_TX          (0x82u)
#define OUTER_PAYLOAD_LENGTH       (11u)
#define OUTER_FRAME_LENGTH         (23u)

#define UDS_REQUEST_CAN_ID         (0x07E0u)
#define UDS_RESPONSE_CAN_ID        (0x07E8u)
#define UDS_RX_BUFFER_SIZE         (272u)
#define UDS_TRANSFER_DATA_MAX      (256u)

#define UDS_SESSION_PROGRAMMING    (0x02u)
#define UDS_ROUTINE_ERASE_MEMORY   (0xFF01u)

#define NRC_SERVICE_NOT_SUPPORTED (0x11u)
#define NRC_SUBFUNCTION_NOT_SUPPORTED (0x12u)
#define NRC_INCORRECT_LENGTH       (0x13u)
#define NRC_REQUEST_SEQUENCE_ERROR (0x24u)
#define NRC_REQUEST_OUT_OF_RANGE   (0x31u)
#define NRC_SECURITY_ACCESS_DENIED (0x33u)
#define NRC_INVALID_KEY            (0x35u)
#define NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED (0x70u)
#define NRC_TRANSFER_DATA_SUSPENDED (0x71u)
#define NRC_GENERAL_PROGRAMMING_FAILURE (0x72u)
#define NRC_WRONG_BLOCK_SEQUENCE   (0x73u)
#define NRC_SUBFUNCTION_NOT_IN_SESSION (0x7Eu)
#define NRC_RESPONSE_PENDING       (0x78u)

#define FLASH_ERROR_FLAGS (FLASH_SR_OPERR | FLASH_SR_WRPERR | \
                           FLASH_SR_PGAERR | FLASH_SR_PGPERR | \
                           FLASH_SR_PGSERR)

typedef struct
{
    uint8_t session;
    uint8_t unlocked;
    uint8_t erased;
    uint8_t download_active;
    uint8_t expected_bsc;
    uint8_t word[4];
    uint8_t word_length;
    uint32_t seed;
    uint32_t address;
    uint32_t expected_length;
    uint32_t received_length;
} DownloadContext_t;

static DownloadContext_t s_download;
static uint8_t s_outer_sequence;
static uint8_t s_isotp_buffer[UDS_RX_BUFFER_SIZE];
static uint16_t s_isotp_length;
static uint16_t s_isotp_received;
static uint8_t s_isotp_next_sn;

static uint16_t crc16_ccitt_false(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFu;
    uint16_t index;
    for (index = 0u; index < length; index++)
    {
        uint8_t bit;
        crc ^= (uint16_t)((uint16_t)data[index] << 8u);
        for (bit = 0u; bit < 8u; bit++)
        {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1u) ^ 0x1021u) :
                                    (uint16_t)(crc << 1u);
        }
    }
    return crc;
}

static void uart_init(void)
{
    uint32_t shift;

    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    (void)RCC->APB2ENR;

    shift = 9UL * 2UL;
    GPIOA->MODER = (GPIOA->MODER & ~(3UL << shift)) | (2UL << shift);
    shift = 10UL * 2UL;
    GPIOA->MODER = (GPIOA->MODER & ~(3UL << shift)) | (2UL << shift);
    GPIOA->OSPEEDR |= (3UL << (9UL * 2UL)) | (3UL << (10UL * 2UL));
    GPIOA->PUPDR = (GPIOA->PUPDR & ~(3UL << (10UL * 2UL))) |
                   (1UL << (10UL * 2UL));
    GPIOA->AFR[1] = (GPIOA->AFR[1] & ~((0xFUL << 4UL) | (0xFUL << 8UL))) |
                    (7UL << 4UL) | (7UL << 8UL);

    USART1->CR1 = 0UL;
    USART1->BRR = (UART_CLOCK_HZ + (UART_BAUD_RATE / 2UL)) / UART_BAUD_RATE;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static uint8_t uart_read_byte(void)
{
    while ((USART1->SR & USART_SR_RXNE) == 0UL) { }
    return (uint8_t)USART1->DR;
}

static void uart_write(const uint8_t *data, uint16_t length)
{
    uint16_t index;
    for (index = 0u; index < length; index++)
    {
        while ((USART1->SR & USART_SR_TXE) == 0UL) { }
        USART1->DR = data[index];
    }
    while ((USART1->SR & USART_SR_TC) == 0UL) { }
}

static void send_can(const uint8_t data[8])
{
    uint8_t frame[OUTER_FRAME_LENGTH] = {0};
    uint16_t crc;

    frame[0] = OUTER_SOF0;
    frame[1] = OUTER_SOF1;
    frame[2] = OUTER_VERSION;
    frame[3] = OUTER_MSG_VCAN_TX;
    frame[4] = 0u;
    frame[5] = s_outer_sequence++;
    frame[6] = OUTER_PAYLOAD_LENGTH;
    frame[7] = 0u;
    frame[8] = (uint8_t)UDS_RESPONSE_CAN_ID;
    frame[9] = (uint8_t)(UDS_RESPONSE_CAN_ID >> 8u);
    frame[10] = 8u;
    (void)memcpy(&frame[11], data, 8u);
    crc = crc16_ccitt_false(&frame[2], 17u);
    frame[19] = (uint8_t)crc;
    frame[20] = (uint8_t)(crc >> 8u);
    frame[21] = OUTER_EOF0;
    frame[22] = OUTER_EOF1;
    uart_write(frame, sizeof(frame));
}

static void send_uds(const uint8_t *payload, uint8_t length)
{
    uint8_t frame[8] = {0};
    if ((payload == 0) || (length > 7u)) return;
    frame[0] = length;
    (void)memcpy(&frame[1], payload, length);
    send_can(frame);
}

static void send_negative(uint8_t sid, uint8_t nrc)
{
    uint8_t response[3] = {0x7Fu, sid, nrc};
    send_uds(response, sizeof(response));
}

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24u) | ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) | data[3];
}

static uint32_t rotl32(uint32_t value, uint8_t amount)
{
    return (value << amount) | (value >> (32u - amount));
}

static uint32_t expected_key(uint32_t seed)
{
    /* Demonstration only. A production ECU must use an OEM-approved security
     * algorithm and rate limiting, normally inside an HSM. */
    return rotl32(seed ^ 0xC35A91E7UL, 5u) + 0x01020304UL;
}

static bool flash_wait(void)
{
    while ((FLASH->SR & FLASH_SR_BSY) != 0UL) { }
    if ((FLASH->SR & FLASH_ERROR_FLAGS) != 0UL) return false;
    if ((FLASH->SR & FLASH_SR_EOP) != 0UL) FLASH->SR = FLASH_SR_EOP;
    return true;
}

static void flash_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0UL)
    {
        FLASH->KEYR = 0x45670123UL;
        FLASH->KEYR = 0xCDEF89ABUL;
    }
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

static bool flash_erase_application(void)
{
    uint32_t sector;
    flash_unlock();
    for (sector = 5UL; sector <= 10UL; sector++)
    {
        if (!flash_wait()) return false;
        FLASH->SR = FLASH_ERROR_FLAGS | FLASH_SR_EOP;
        FLASH->CR = (FLASH->CR & ~(FLASH_CR_SNB | FLASH_CR_PSIZE)) |
                    FLASH_CR_SER | FLASH_CR_PSIZE_1 |
                    (sector << FLASH_CR_SNB_Pos);
        FLASH->CR |= FLASH_CR_STRT;
        if (!flash_wait())
        {
            FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
            return false;
        }
        FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    }
    return true;
}

static bool flash_program_word(uint32_t address, const uint8_t bytes[4])
{
    uint32_t value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
                     ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
    if (!flash_wait()) return false;
    FLASH->SR = FLASH_ERROR_FLAGS | FLASH_SR_EOP;
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PSIZE) | FLASH_CR_PSIZE_1 | FLASH_CR_PG;
    *((volatile uint32_t *)address) = value;
    if (!flash_wait())
    {
        FLASH->CR &= ~FLASH_CR_PG;
        return false;
    }
    FLASH->CR &= ~FLASH_CR_PG;
    return *((volatile uint32_t *)address) == value;
}

static bool stream_program(const uint8_t *data, uint16_t length)
{
    uint16_t index;
    for (index = 0u; index < length; index++)
    {
        s_download.word[s_download.word_length++] = data[index];
        if (s_download.word_length == 4u)
        {
            uint32_t word_address = s_download.address +
                                    s_download.received_length - 3UL;
            if (!flash_program_word(word_address, s_download.word)) return false;
            s_download.word_length = 0u;
        }
        s_download.received_length++;
    }
    return true;
}

static bool flush_last_word(void)
{
    uint32_t word_address;
    if (s_download.word_length == 0u) return true;
    word_address = s_download.address + s_download.received_length -
                   s_download.word_length;
    while (s_download.word_length < 4u)
    {
        s_download.word[s_download.word_length++] = 0xFFu;
    }
    if (!flash_program_word(word_address, s_download.word)) return false;
    s_download.word_length = 0u;
    return true;
}

static bool program_manifest(uint32_t image_crc, uint32_t version)
{
    BootImageManifest_t manifest;
    const uint8_t *raw = (const uint8_t *)&manifest;
    uint32_t offset;

    manifest.magic = BOOT_IMAGE_MAGIC;
    manifest.format_version = BOOT_MANIFEST_FORMAT_VERSION;
    manifest.image_start = BOOT_APPLICATION_BASE_ADDRESS;
    manifest.image_size = s_download.expected_length;
    manifest.image_crc32 = image_crc;
    manifest.software_version = version;
    manifest.flags = 0UL;
    manifest.manifest_crc32 = BootImage_Crc32(raw, 28UL);
    for (offset = 0UL; offset < sizeof(manifest); offset += 4UL)
    {
        if (!flash_program_word(BOOT_MANIFEST_ADDRESS + offset, &raw[offset]))
        {
            return false;
        }
    }
    return true;
}

static bool in_programming_session(uint8_t sid)
{
    if (s_download.session != UDS_SESSION_PROGRAMMING)
    {
        send_negative(sid, NRC_SUBFUNCTION_NOT_IN_SESSION);
        return false;
    }
    return true;
}

static void handle_uds(const uint8_t *request, uint16_t length)
{
    uint8_t sid;
    uint8_t response[7];
    if ((request == 0) || (length == 0u)) return;
    sid = request[0];

    if (sid == 0x10u)
    {
        if (length != 2u) { send_negative(sid, NRC_INCORRECT_LENGTH); return; }
        if ((request[1] & 0x7Fu) != UDS_SESSION_PROGRAMMING)
        {
            send_negative(sid, NRC_SUBFUNCTION_NOT_SUPPORTED); return;
        }
        s_download.session = UDS_SESSION_PROGRAMMING;
        response[0] = 0x50u; response[1] = UDS_SESSION_PROGRAMMING;
        response[2] = 0u; response[3] = 50u; response[4] = 1u; response[5] = 244u;
        send_uds(response, 6u);
    }
    else if (sid == 0x27u)
    {
        if (!in_programming_session(sid)) return;
        if (length == 2u && request[1] == 0x01u)
        {
            const uint32_t *uid = (const uint32_t *)UID_BASE;
            s_download.seed = uid[0] ^ uid[1] ^ uid[2] ^ 0x6B61796FUL;
            if (s_download.seed == 0UL) s_download.seed = 0x13579BDFUL;
            s_download.unlocked = 0u;
            response[0] = 0x67u; response[1] = 0x01u;
            response[2] = (uint8_t)(s_download.seed >> 24u);
            response[3] = (uint8_t)(s_download.seed >> 16u);
            response[4] = (uint8_t)(s_download.seed >> 8u);
            response[5] = (uint8_t)s_download.seed;
            send_uds(response, 6u);
        }
        else if (length == 6u && request[1] == 0x02u)
        {
            if ((s_download.seed == 0UL) ||
                (read_be32(&request[2]) != expected_key(s_download.seed)))
            {
                send_negative(sid, NRC_INVALID_KEY); return;
            }
            s_download.unlocked = 1u;
            s_download.seed = 0UL;
            response[0] = 0x67u; response[1] = 0x02u;
            send_uds(response, 2u);
        }
        else send_negative(sid, NRC_INCORRECT_LENGTH);
    }
    else if (sid == 0x31u)
    {
        uint8_t pending[3] = {0x7Fu, 0x31u, NRC_RESPONSE_PENDING};
        if (!in_programming_session(sid)) return;
        if (!s_download.unlocked) { send_negative(sid, NRC_SECURITY_ACCESS_DENIED); return; }
        if (length != 4u) { send_negative(sid, NRC_INCORRECT_LENGTH); return; }
        if (request[1] != 0x01u || request[2] != 0xFFu || request[3] != 0x01u)
        { send_negative(sid, NRC_REQUEST_OUT_OF_RANGE); return; }
        send_uds(pending, sizeof(pending));
        if (!flash_erase_application())
        { flash_lock(); send_negative(sid, NRC_GENERAL_PROGRAMMING_FAILURE); return; }
        s_download.erased = 1u;
        s_download.download_active = 0u;
        response[0] = 0x71u; response[1] = 0x01u;
        response[2] = 0xFFu; response[3] = 0x01u; response[4] = 0u;
        send_uds(response, 5u);
    }
    else if (sid == 0x34u)
    {
        uint32_t address;
        uint32_t image_length;
        if (!in_programming_session(sid)) return;
        if (!s_download.unlocked) { send_negative(sid, NRC_SECURITY_ACCESS_DENIED); return; }
        if (!s_download.erased) { send_negative(sid, NRC_REQUEST_SEQUENCE_ERROR); return; }
        if (length != 11u || request[1] != 0u || request[2] != 0x44u)
        { send_negative(sid, NRC_INCORRECT_LENGTH); return; }
        address = read_be32(&request[3]);
        image_length = read_be32(&request[7]);
        if (address != BOOT_APPLICATION_BASE_ADDRESS || image_length < 8UL ||
            image_length > (BOOT_APPLICATION_END_ADDRESS - BOOT_APPLICATION_BASE_ADDRESS))
        { send_negative(sid, NRC_UPLOAD_DOWNLOAD_NOT_ACCEPTED); return; }
        s_download.address = address;
        s_download.expected_length = image_length;
        s_download.received_length = 0UL;
        s_download.word_length = 0u;
        s_download.expected_bsc = 1u;
        s_download.download_active = 1u;
        response[0] = 0x74u; response[1] = 0x20u;
        response[2] = (uint8_t)((UDS_TRANSFER_DATA_MAX + 2u) >> 8u);
        response[3] = (uint8_t)(UDS_TRANSFER_DATA_MAX + 2u);
        send_uds(response, 4u);
    }
    else if (sid == 0x36u)
    {
        uint16_t data_length;
        if (!in_programming_session(sid)) return;
        if (!s_download.download_active)
        { send_negative(sid, NRC_REQUEST_SEQUENCE_ERROR); return; }
        if (length < 3u || length > UDS_TRANSFER_DATA_MAX + 2u)
        { send_negative(sid, NRC_INCORRECT_LENGTH); return; }
        if (request[1] != s_download.expected_bsc)
        { send_negative(sid, NRC_WRONG_BLOCK_SEQUENCE); return; }
        data_length = (uint16_t)(length - 2u);
        if (s_download.received_length + data_length > s_download.expected_length)
        { send_negative(sid, NRC_TRANSFER_DATA_SUSPENDED); return; }
        if (!stream_program(&request[2], data_length))
        { flash_lock(); send_negative(sid, NRC_GENERAL_PROGRAMMING_FAILURE); return; }
        response[0] = 0x76u; response[1] = request[1];
        s_download.expected_bsc++;
        send_uds(response, 2u);
    }
    else if (sid == 0x37u)
    {
        uint32_t expected_crc;
        uint32_t version;
        if (!in_programming_session(sid)) return;
        if (!s_download.download_active)
        { send_negative(sid, NRC_REQUEST_SEQUENCE_ERROR); return; }
        if (length != 9u) { send_negative(sid, NRC_INCORRECT_LENGTH); return; }
        if (s_download.received_length != s_download.expected_length)
        { send_negative(sid, NRC_TRANSFER_DATA_SUSPENDED); return; }
        expected_crc = read_be32(&request[1]);
        version = read_be32(&request[5]);
        if (!flush_last_word() ||
            BootImage_Crc32((const uint8_t *)BOOT_APPLICATION_BASE_ADDRESS,
                            s_download.expected_length) != expected_crc ||
            !program_manifest(expected_crc, version))
        { flash_lock(); send_negative(sid, NRC_GENERAL_PROGRAMMING_FAILURE); return; }
        flash_lock();
        s_download.download_active = 0u;
        if (BootImage_Validate() != BOOT_IMAGE_VALID)
        { send_negative(sid, NRC_GENERAL_PROGRAMMING_FAILURE); return; }
        response[0] = 0x77u;
        send_uds(response, 1u);
        NVIC_SystemReset();
    }
    else if (sid == 0x3Eu)
    {
        if (length != 2u || (request[1] & 0x7Fu) != 0u)
        { send_negative(sid, NRC_INCORRECT_LENGTH); return; }
        if ((request[1] & 0x80u) == 0u)
        { response[0] = 0x7Eu; response[1] = 0u; send_uds(response, 2u); }
    }
    else send_negative(sid, NRC_SERVICE_NOT_SUPPORTED);
}

static void process_can(const uint8_t data[8])
{
    uint8_t type = (uint8_t)(data[0] & 0xF0u);
    if (type == 0x00u)
    {
        uint8_t length = (uint8_t)(data[0] & 0x0Fu);
        if (length >= 1u && length <= 7u) handle_uds(&data[1], length);
    }
    else if (type == 0x10u)
    {
        s_isotp_length = (uint16_t)(((uint16_t)(data[0] & 0x0Fu) << 8u) | data[1]);
        if (s_isotp_length < 8u || s_isotp_length > UDS_RX_BUFFER_SIZE)
        {
            s_isotp_length = 0u;
            return;
        }
        (void)memcpy(s_isotp_buffer, &data[2], 6u);
        s_isotp_received = 6u;
        s_isotp_next_sn = 1u;
        {
            uint8_t fc[8] = {0x30u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
            send_can(fc);
        }
    }
    else if (type == 0x20u && s_isotp_length != 0u)
    {
        uint16_t remaining;
        uint8_t chunk;
        if ((data[0] & 0x0Fu) != s_isotp_next_sn)
        {
            s_isotp_length = 0u;
            return;
        }
        remaining = (uint16_t)(s_isotp_length - s_isotp_received);
        chunk = (remaining > 7u) ? 7u : (uint8_t)remaining;
        (void)memcpy(&s_isotp_buffer[s_isotp_received], &data[1], chunk);
        s_isotp_received = (uint16_t)(s_isotp_received + chunk);
        s_isotp_next_sn = (uint8_t)((s_isotp_next_sn + 1u) & 0x0Fu);
        if (s_isotp_received == s_isotp_length)
        {
            uint16_t complete_length = s_isotp_length;
            s_isotp_length = 0u;
            handle_uds(s_isotp_buffer, complete_length);
        }
    }
}

static void receive_outer_frame(void)
{
    uint8_t frame[OUTER_FRAME_LENGTH];
    uint16_t crc;
    uint8_t state = 0u;
    uint8_t index = 0u;

    for (;;)
    {
        uint8_t byte = uart_read_byte();
        if (state == 0u)
        {
            if (byte == OUTER_SOF0) { frame[0] = byte; state = 1u; }
        }
        else if (state == 1u)
        {
            if (byte == OUTER_SOF1)
            { frame[1] = byte; index = 2u; state = 2u; }
            else state = (byte == OUTER_SOF0) ? 1u : 0u;
        }
        else
        {
            frame[index++] = byte;
            if (index != OUTER_FRAME_LENGTH) continue;
            state = 0u;
            if (frame[2] != OUTER_VERSION || frame[3] != OUTER_MSG_VCAN_RX ||
                frame[6] != OUTER_PAYLOAD_LENGTH || frame[7] != 0u ||
                frame[21] != OUTER_EOF0 || frame[22] != OUTER_EOF1) continue;
            crc = crc16_ccitt_false(&frame[2], 17u);
            if (frame[19] != (uint8_t)crc || frame[20] != (uint8_t)(crc >> 8u)) continue;
            if ((((uint16_t)frame[9] << 8u) | frame[8]) != UDS_REQUEST_CAN_ID ||
                frame[10] != 8u) continue;
            process_can(&frame[11]);
            return;
        }
    }
}

bool BootDiag_ConsumeRequest(void)
{
    volatile uint32_t *token = (volatile uint32_t *)BOOT_REQUEST_ADDRESS;
    bool requested = (*token == BOOT_REQUEST_MAGIC);
    *token = 0UL;
    __DSB();
    return requested;
}

void BootDiag_Run(void)
{
    (void)memset(&s_download, 0, sizeof(s_download));
    uart_init();
    for (;;) receive_outer_frame();
}
