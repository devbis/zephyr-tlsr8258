/* SPDX-License-Identifier: Apache-2.0 */

#include "ieee802154_native_sim_socket_bottom.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "nsi_errno.h"

int ieee802154_native_sim_socket_open(const char *host, uint16_t port)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(port),
	};
	int fd;
	int flags;

	if (host == NULL || inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		return -NSI_ERRNO_MID_EINVAL;
	}

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		return -nsi_errno_to_mid(errno);
	}

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		int err = errno;

		(void)close(fd);
		return -nsi_errno_to_mid(err);
	}

	if (connect(fd, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
		int err = errno;

		(void)close(fd);
		return -nsi_errno_to_mid(err);
	}

	return fd;
}
