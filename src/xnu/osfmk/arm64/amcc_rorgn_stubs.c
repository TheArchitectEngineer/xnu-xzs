#include <stdint.h>
#include <stdbool.h>
#include <mach/vm_types.h>
#include <arm64/amcc_rorgn.h>
#include <kern/ast.h>

vm_offset_t rorgn_begin = 0;
vm_offset_t rorgn_end = 0;

void
rorgn_stash_range(void)
{
}

void
rorgn_lockdown(void)
{
}

bool
rorgn_contains(vm_offset_t addr, vm_size_t size, bool defval)
{
	(void)addr;
	(void)size;
	return defval;
}

void
rorgn_validate_core(void)
{
}

void
ml_enable_monitor(void)
{
}

bool
pmap_pending_preemption(void)
{
	return !!(*((volatile ast_t *)ast_pending()) & AST_URGENT);
}
