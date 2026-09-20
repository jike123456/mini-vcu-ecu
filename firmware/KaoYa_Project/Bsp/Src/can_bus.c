#include "can_bus.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "stm32f4xx.h"
#include "stm32f4xx_hal.h"
#include "task.h"

#include <string.h>

#define CAN_RX_QUEUE_DEPTH       16U
#define CAN_INIT_TIMEOUT_MS      100U

static QueueHandle_t s_rx_queue;
static StaticQueue_t s_rx_queue_control;
static uint8_t s_rx_queue_storage[CAN_RX_QUEUE_DEPTH * sizeof(CanBusFrame_t)];
static volatile CanBusStats_t s_stats;
static bool s_initialized;

static void prvSnapshotRegisters(void)
{
    s_stats.mcr = CAN1->MCR;
    s_stats.msr = CAN1->MSR;
    s_stats.tsr = CAN1->TSR;
}

static bool prvWaitForBit(volatile uint32_t *reg, uint32_t mask, bool set)
{
    uint32_t start_tick = HAL_GetTick();

    for (;;) {
        bool current = ((*reg & mask) != 0U);
        if (current == set) {
            return true;
        }
        if ((HAL_GetTick() - start_tick) >= CAN_INIT_TIMEOUT_MS) {
            return false;
        }
    }
}

static void prvConfigurePins(void)
{
    /* PA11 = CAN1_RX, PA12 = CAN1_TX, alternate function AF9. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    (void)RCC->AHB1ENR;

    GPIOA->MODER &= ~((3UL << (11U * 2U)) | (3UL << (12U * 2U)));
    GPIOA->MODER |=  ((2UL << (11U * 2U)) | (2UL << (12U * 2U)));
    GPIOA->OTYPER &= ~((1UL << 11U) | (1UL << 12U));
    GPIOA->OSPEEDR |= ((3UL << (11U * 2U)) | (3UL << (12U * 2U)));
    GPIOA->PUPDR &= ~((3UL << (11U * 2U)) | (3UL << (12U * 2U)));
    /* Keep CAN_RX recessive even when the board CAN/USB selector is open. */
    GPIOA->PUPDR |=  (1UL << (11U * 2U));

    GPIOA->AFR[1] &= ~((0xFUL << 12U) | (0xFUL << 16U));
    GPIOA->AFR[1] |=  ((9UL << 12U) | (9UL << 16U));
}

static void prvConfigureAcceptAllFilter(void)
{
    /* Filter bank 0: 32-bit identifier-mask mode, all-zero mask, FIFO0. */
    CAN1->FMR |= CAN_FMR_FINIT;
    CAN1->FA1R &= ~1UL;
    CAN1->FM1R &= ~1UL;
    CAN1->FS1R |= 1UL;
    CAN1->FFA1R &= ~1UL;
    CAN1->sFilterRegister[0].FR1 = 0UL;
    CAN1->sFilterRegister[0].FR2 = 0UL;
    CAN1->FA1R |= 1UL;
    CAN1->FMR &= ~CAN_FMR_FINIT;
}

bool CanBus_Init(void)
{
    const CanBusStats_t cleared_stats = {0};

    if (s_initialized) {
        return true;
    }

    s_stats = cleared_stats;
    if (s_rx_queue == NULL) {
        s_rx_queue = xQueueCreateStatic(CAN_RX_QUEUE_DEPTH,
                                        sizeof(CanBusFrame_t),
                                        s_rx_queue_storage,
                                        &s_rx_queue_control);
        if (s_rx_queue == NULL) {
            s_stats.init_status = CAN_BUS_INIT_QUEUE_FAILED;
            return false;
        }
    } else {
        (void)xQueueReset(s_rx_queue);
    }

    prvConfigurePins();

    RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;
    (void)RCC->APB1ENR;

    /* Start from a known peripheral state, including after a debugger reset. */
    RCC->APB1RSTR |= RCC_APB1RSTR_CAN1RST;
    (void)RCC->APB1RSTR;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_CAN1RST;
    (void)RCC->APB1RSTR;

    /* Follow the STM32F4 HAL/bxCAN state-machine sequence exactly. */
    CAN1->MCR |= CAN_MCR_INRQ;
    if (!prvWaitForBit(&CAN1->MSR, CAN_MSR_INAK, true)) {
        s_stats.init_status = CAN_BUS_INIT_ENTER_TIMEOUT;
        prvSnapshotRegisters();
        return false;
    }

    CAN1->MCR &= ~CAN_MCR_SLEEP;
    if (!prvWaitForBit(&CAN1->MSR, CAN_MSR_SLAK, false)) {
        s_stats.init_status = CAN_BUS_INIT_SLEEP_TIMEOUT;
        prvSnapshotRegisters();
        return false;
    }

    /*
     * PCLK1 = 42 MHz. 500 kbit/s = 42 MHz / (6 * (1 + 11 + 2)).
     * Sample point = (1 + 11) / 14 = 85.7 percent.
     */
    CAN1->MCR = CAN_MCR_INRQ | CAN_MCR_ABOM | CAN_MCR_TXFP;
    CAN1->BTR = (5UL << CAN_BTR_BRP_Pos) |
                (10UL << CAN_BTR_TS1_Pos) |
                (1UL << CAN_BTR_TS2_Pos) |
                (0UL << CAN_BTR_SJW_Pos);
#if CAN_BUS_INTERNAL_LOOPBACK
    CAN1->BTR |= CAN_BTR_LBKM;
#endif

    prvConfigureAcceptAllFilter();

    CAN1->MCR &= ~CAN_MCR_INRQ;
    if (!prvWaitForBit(&CAN1->MSR, CAN_MSR_INAK, false)) {
        s_stats.init_status = CAN_BUS_INIT_LEAVE_TIMEOUT;
        prvSnapshotRegisters();
        return false;
    }

    CAN1->IER = CAN_IER_FMPIE0 | CAN_IER_ERRIE | CAN_IER_BOFIE | CAN_IER_LECIE;

    NVIC_SetPriority(CAN1_RX0_IRQn, 5U);
    NVIC_ClearPendingIRQ(CAN1_RX0_IRQn);
    NVIC_EnableIRQ(CAN1_RX0_IRQn);
    NVIC_SetPriority(CAN1_SCE_IRQn, 5U);
    NVIC_ClearPendingIRQ(CAN1_SCE_IRQn);
    NVIC_EnableIRQ(CAN1_SCE_IRQn);

    s_stats.init_status = CAN_BUS_INIT_READY;
    s_initialized = true;
    prvSnapshotRegisters();
    return true;
}

bool CanBus_SendStd(uint16_t std_id, const uint8_t *data, uint8_t dlc)
{
    CAN_TxMailBox_TypeDef *mailbox;
    uint32_t tsr;
    uint32_t low = 0U;
    uint32_t high = 0U;
    uint8_t i;

    if ((std_id > 0x7FFU) || (dlc > 8U) || ((dlc > 0U) && (data == NULL))) {
        s_stats.tx_fail++;
        return false;
    }

    tsr = CAN1->TSR;
    if ((tsr & CAN_TSR_TME0) != 0U) {
        mailbox = &CAN1->sTxMailBox[0];
    } else if ((tsr & CAN_TSR_TME1) != 0U) {
        mailbox = &CAN1->sTxMailBox[1];
    } else if ((tsr & CAN_TSR_TME2) != 0U) {
        mailbox = &CAN1->sTxMailBox[2];
    } else {
        s_stats.tx_fail++;
        return false;
    }

    for (i = 0U; (i < dlc) && (i < 4U); i++) {
        low |= ((uint32_t)data[i] << (8U * i));
    }
    for (i = 4U; i < dlc; i++) {
        high |= ((uint32_t)data[i] << (8U * (i - 4U)));
    }

    mailbox->TIR = ((uint32_t)std_id << 21U);
    mailbox->TDTR = (uint32_t)dlc;
    mailbox->TDLR = low;
    mailbox->TDHR = high;
    mailbox->TIR |= CAN_TI0R_TXRQ;

    s_stats.tx_ok++;
    prvSnapshotRegisters();
    return true;
}

bool CanBus_Receive(CanBusFrame_t *frame, uint32_t timeout_ms)
{
    if ((s_rx_queue == NULL) || (frame == NULL)) {
        return false;
    }

    return xQueueReceive(s_rx_queue, frame, pdMS_TO_TICKS(timeout_ms)) == pdPASS;
}

void CanBus_GetStats(CanBusStats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    taskENTER_CRITICAL();
    prvSnapshotRegisters();
    *stats = s_stats;
    taskEXIT_CRITICAL();
}

void CanBus_Rx0IrqHandler(void)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    while ((CAN1->RF0R & CAN_RF0R_FMP0) != 0U) {
        const CAN_FIFOMailBox_TypeDef *mailbox = &CAN1->sFIFOMailBox[0];
        uint32_t rir = mailbox->RIR;
        uint32_t rdtr = mailbox->RDTR;
        uint32_t low = mailbox->RDLR;
        uint32_t high = mailbox->RDHR;
        CanBusFrame_t frame;

        if (((rir & CAN_RI0R_IDE) == 0U) && ((rir & CAN_RI0R_RTR) == 0U)) {
            frame.std_id = (uint16_t)((rir >> 21U) & 0x7FFU);
            frame.dlc = (uint8_t)(rdtr & 0x0FU);
            if (frame.dlc > 8U) {
                frame.dlc = 8U;
            }
            frame.data[0] = (uint8_t)(low);
            frame.data[1] = (uint8_t)(low >> 8U);
            frame.data[2] = (uint8_t)(low >> 16U);
            frame.data[3] = (uint8_t)(low >> 24U);
            frame.data[4] = (uint8_t)(high);
            frame.data[5] = (uint8_t)(high >> 8U);
            frame.data[6] = (uint8_t)(high >> 16U);
            frame.data[7] = (uint8_t)(high >> 24U);

            if (xQueueSendFromISR(s_rx_queue, &frame, &higher_priority_task_woken) == pdPASS) {
                s_stats.rx_ok++;
            } else {
                s_stats.rx_drop++;
            }
        } else {
            s_stats.rx_drop++;
        }

        CAN1->RF0R |= CAN_RF0R_RFOM0;
    }

    portYIELD_FROM_ISR(higher_priority_task_woken);
}

void CanBus_ErrorIrqHandler(void)
{
    s_stats.last_esr = CAN1->ESR;
    s_stats.error_irq++;
    CAN1->MSR &= ~CAN_MSR_ERRI;
}
