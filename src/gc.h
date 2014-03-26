#include "cpu.h"
#include <atomic>
#include <functional>
#include <cassert>

class gc_cpu;

// Derive from this class to make a garbage collected object. Call
// gc_free() method to schedule for deletion.
class gc_object
{
private:
  // List of objects waiting to be seen.
  std::atomic<gc_object *> next;
  // Mask of cpus which have seen this object.
  std::atomic<cpu_mask_t> seen;
  bool scheduled;               // XXX - debugging
  bool dispatched;              // XXX - debugging

  friend gc_cpu;

  // Update seen state.
  void observe(int cpu);

  gc_object(const gc_object &) = delete;

  void set_next(gc_object *nxt) {
    assert(nxt == nullptr || nxt > (void*)0x1000);
    next = nxt;
  }
  
public:
  gc_object() : next(nullptr), seen(0),
                scheduled(false), dispatched(false) { }
  virtual ~gc_object() { }
  
  void gc_free();
};

// Called periodically by threads to notify that they are not
// referencing any gc objects.
void gc_checkpoint();
// Block until all threads have checkpointed.
void gc_flush();

void gc_exit();

void gc_finish();

void gc_lock();
void gc_unlock();
