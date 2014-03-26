#include "cpu.h"
#include <atomic>
#include <cassert>

static std::atomic<cpu_mask_t> _cpu_mask(0);
static std::atomic<int> _cpu_count(0);
static __thread int _cpu_id;

void
cpu_init()
{
  _cpu_id = _cpu_count++;
  _cpu_mask |= 1ULL << _cpu_id;
  assert(_cpu_id < MAX_CPUS);
}

cpu_mask_t
cpu_mask_all()
{
  //XXX - XXX
  //return (1ULL << _cpu_count.load()) - 1;
  return _cpu_mask;
}

int 
cpu_id()
{
  return _cpu_id;
}

int
cpu_count()
{
  return _cpu_count;
}

void
cpu_exit()
{
  _cpu_mask &= ~(1ULL << cpu_id());
}

bool
cpu_seen_all(cpu_mask_t seen)
{
  cpu_mask_t cpu = _cpu_mask;
  if (~seen & cpu)
    return false;
  else
    return true;
}
