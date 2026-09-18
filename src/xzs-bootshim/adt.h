#ifndef ADT_H
#define ADT_H

#include <stdint.h>
#include <stddef.h>

#define ADT_BASE_ADDR 0x81810000UL

/* Builds a minimal Apple Device Tree in RAM at a specified physical address.
 * Returns the total length in bytes.
 */
uint32_t adt_build_tree_at(uint64_t base_addr);
uint32_t adt_build_tree(void);

#endif /* ADT_H */
