/********************************************************************************************************
 * @file    utlist.h
 *
 * @brief   This is the header file for utlist
 *
 * @author  Driver & Zigbee Group
 * @date    2021
 *
 * @par     Copyright (c) 2021, Telink Semiconductor (Shanghai) Co., Ltd. ("TELINK")
 *          All rights reserved.
 *
 *          Licensed under the Apache License, Version 2.0 (the "License");
 *          you may not use this file except in compliance with the License.
 *          You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 *          Unless required by applicable law or agreed to in writing, software
 *          distributed under the License is distributed on an "AS IS" BASIS,
 *          WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *          See the License for the specific language governing permissions and
 *          limitations under the License.
 *
 *******************************************************************************************************/
#ifndef UTLIST_H
#define UTLIST_H

/*
 * Defensive walk cap for every traversal below. A corrupted/cyclic chain
 * (e.g. a double LIST_ADD of a node already reachable from head) turns any
 * of these unbounded `while`/`for` walks into a permanent hang -- confirmed
 * on hardware: mac_sendTxCnf() -> ev_timer_taskCancel()/ev_timer_taskPost()
 * on mac_ed_poll_rx_window_evt (mac_trx.c) called LIST_EXIST/LIST_DELETE
 * during an interview TX-confirm retry storm and froze the whole ZB thread
 * forever (heartbeat + every counter in the loop stopped advancing, with no
 * CPU fault -- a plain infinite loop, not a crash). ev_timer_zephyr.c's own
 * ev_timer_execute_cb()/ev_timer_nearest_update() already learned this
 * lesson and cap their walks at EV_TIMER_MAX_WALK; give these generic list
 * macros the same backstop instead of trusting every caller to never
 * double-insert a node. Set well above the real list sizes in this codebase
 * (the ev_timer pool is 24 entries plus a handful of runtime ones) so it
 * only ever fires on genuine corruption.
 */
#define UTLIST_MAX_WALK 128U

#define LIST_ADD(head, ele)             do{ \
                                            (ele)->next = head; \
                                            head = ele;         \
                                        }while(0)

#define LIST_DELETE(head, ele)          do{ \
                                            __typeof(head) lst;                                 \
                                            unsigned int _utlist_walk = 0U;                     \
                                            if ((head) == (ele)) {                              \
                                                (head) = (head)->next;                          \
                                            } else {                                            \
                                                lst = head;                                     \
                                                while (lst->next && (lst->next != (ele)) &&      \
                                                       _utlist_walk < UTLIST_MAX_WALK) {         \
                                                    lst = lst->next;                            \
                                                    _utlist_walk++;                             \
                                                }                                               \
                                                if (lst->next && _utlist_walk < UTLIST_MAX_WALK) {\
                                                    lst->next = ((ele)->next);                  \
                                                }                                               \
                                            }                                                   \
                                        }while(0)

#define LIST_EXIST(head, ele, result)   do{ \
                                            unsigned int _utlist_walk = 0U;                              \
                                            for (result = head; result && _utlist_walk < UTLIST_MAX_WALK; \
                                                 result = (result)->next, _utlist_walk++) {      \
                                                if (result == ele) {    \
                                                    break;              \
                                                }                       \
                                            }                           \
                                        }while(0)

#endif /* UTLIST_H */
