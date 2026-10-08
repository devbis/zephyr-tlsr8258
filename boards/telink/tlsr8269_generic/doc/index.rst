.. _tlsr8269_generic:

Telink TLSR8269 Generic
#######################

Overview
********

Generic TLSR8269 board definition (826x series). Bring-up is in progress:
the SoC description, GPIO and flash are present; UART, the 802.15.4 radio
and Zigbee are not yet ported.

Hardware
********

- TLSR8269 SoC, TC32 core at 24 MHz system clock
- 512 KB flash
- 32 KB SRAM at 0x808000

Flashing
********

Flash with TlsrPgm (``west flash``). probe-rs has no TLSR8269 target.
