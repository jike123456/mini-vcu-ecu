Mini VCU application firmware

This application extends the Kaoya Robot STM32F407 / FreeRTOS framework.
Original authorship, copyright notices and third-party licenses are retained.

Project overview: ../../../README.md
Architecture and verification reports: ../../../docs/
Source and license information: ../../../THIRD_PARTY_NOTICES.md

The default control path uses a software motor model and UART virtual CAN.
CAN controller loopback does not establish physical two-node CAN validation.
Read the Bootloader memory layout before programming a relocated application.
