/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef LOADER_UPDATE_H_
#define LOADER_UPDATE_H_

/**
 * @brief Bring the loader in the flash to the one of the payload
 *
 * @retval 1 The loader was rewritten
 * @retval 0 The loader is the same
 * @retval -ENODEV The flash is not ready
 * @retval -EIO An erase or a write failed, the loader may be incomplete
 */
int loader_update(void);

#endif /* LOADER_UPDATE_H_ */
