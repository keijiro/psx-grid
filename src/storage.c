/*
 * storage.c - C initialization ABI for Rust score storage
 *
 * Implementation notes:
 *
 * The MIPS C ABI passes CardBackend by value differently from Rust. This
 * adapter forwards a pointer while preserving the public initialization API.
 */

#include "storage.h"

_Static_assert(sizeof(CardFile) == 28, "Rust CardFile ABI changed");

extern void storage_init_rust(Storage* storage, const CardBackend* backend);

void storage_init(Storage* storage, CardBackend backend)
{
    storage_init_rust(storage, &backend);
}
