#include <cstdint>

#define MAX_CPUS (32)

typedef uint64_t cpu_mask_t;
cpu_mask_t cpu_mask_all();

void cpu_init();

int cpu_id();
int cpu_count();
void cpu_exit();
bool cpu_seen_all(cpu_mask_t seen);
