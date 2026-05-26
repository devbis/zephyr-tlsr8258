#include "ieee802154_tlsr8258_tx_irq.h"

bool tlsr8258_tx_irq_indicates_success(uint16_t irq)
{
	const uint16_t success_irqs = (uint16_t)((1u << 1) | (1u << 8));

	return (irq & success_irqs) != 0u;
}
