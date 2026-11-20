// Host test for fz_table(): fixed-address state table, created once, reused, never on a foreign mapping.
// Compiles blob.c in host mode (real mach calls, test address).
//
// Run from the repository root:
//   clang -std=c11 -DFZ_HOST_TEST -DFZ_TABLE_ADDR=0x580000000000ULL Tests/freeze_table_test.c -o /tmp/fz_table && /tmp/fz_table

#include "../CASyncFix/freeze_blob/blob.c"

#include <stdio.h>

static int failures = 0;
static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    failures += ok ? 0 : 1;
}

int main(void) {
    // fresh_allocates_and_inits
    FzTable *t = fz_table();
    check(t == (FzTable *)FZ_TABLE_ADDR && t->magic == FZ_MAGIC && t->mode == fz_config.mode &&
          t->budgetNs == fz_config.budgetNs, "fresh_allocates_and_inits");

    // second_call_reuses (state survives)
    if (t) t->n = 7;
    FzTable *t2 = fz_table();
    check(t2 == t && t2 && t2->n == 7, "second_call_reuses");

    // foreign_mapping_rejected: a region with another tag at the address -> 0, region untouched
    mach_vm_address_t foreign = FZ_TABLE_ADDR + 0x100000000ULL;
    kern_return_t kr = mach_vm_allocate(mach_task_self(), &foreign, 0x4000, VM_FLAGS_FIXED | VM_MAKE_TAG(241));
    *(volatile uint32_t *)foreign = 0x12345678;
    check(kr == KERN_SUCCESS && fz_table_at(foreign) == 0 && *(volatile uint32_t *)foreign == 0x12345678,
          "foreign_mapping_rejected");

    printf(failures ? "\n%d FAILED\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
