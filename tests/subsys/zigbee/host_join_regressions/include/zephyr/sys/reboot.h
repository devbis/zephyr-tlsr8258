/* SPDX-License-Identifier: Apache-2.0 */
/* Host stand-in for <zephyr/sys/reboot.h>. */
#ifndef ZIGBEE_HOST_REBOOT_H
#define ZIGBEE_HOST_REBOOT_H

#define SYS_REBOOT_COLD 1

void sys_reboot(int type);

#endif /* ZIGBEE_HOST_REBOOT_H */
