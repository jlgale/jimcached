/* Garbage Collection
 *
 * Objects can be garbage collected when its guaranteed that no thread
 * has a reference to the object.
 *
 * GC objects transiton through a lattice of states each time they are
 * observed by a CPU as having been freed. When all CPUs have observed
 * that the object is freed (top), delete is invoked on the object.
 *
 * When gc_free() is called, the object is placed on a pending queue,
 * private to the invoking CPU, in the order which it was freed.  We
 * can infer that an object was observed by a CPU if an object freed
 * later than it was observed by that CPU.
 */
#include "gc.h"
#include <cstddef>
#include <cassert>
#include <mutex>
#include <condition_variable>

class gc_flush_control
{
private:

  class waiter
  {
  private:
    std::unique_lock<std::mutex> lock;
    cpu_mask_t seen;
    int id;
    std::condition_variable ready;
    waiter *next;
  public:
    waiter(std::mutex &m)
      : lock(m), seen(0), id(cpu_id()), next(nullptr) { }
    waiter *get_next() const { return next; }
    void append(waiter *w) {
      if (next != nullptr)
        next->append(w);
      else
        next = w;
    }
    void wait() {
      while (!cpu_seen_all(seen))
        ready.wait(lock);
    }
    void checkpoint() {
      seen |= 1ULL << cpu_id();
      if (cpu_seen_all(seen))
        ready.notify_all();

      if (next != nullptr)
        next->checkpoint();
    }
  };

  std::atomic<waiter *> flushes; // Pending flushes, oldest to youngest
  std::mutex mutex;
  void checkpoint_locked();
  
public:
  gc_flush_control() : flushes(nullptr) { }
  void checkpoint();
  void force_checkpoint();
  void flush();
};

void gc_flush_control::flush()
{
  gc_checkpoint();
  waiter w(mutex);
  waiter *head = flushes;
  if (head == nullptr) {
    flushes = &w;
  } else {
    head->append(&w);
  }
  checkpoint_locked();
  w.wait();
  // XXX - flushes might not be &w!
  flushes = w.get_next();
}

void gc_flush_control::checkpoint_locked()
{
  waiter *w = flushes;
  if (w != nullptr) 
    w->checkpoint();
}

void gc_flush_control::force_checkpoint()
{
  std::unique_lock<std::mutex> lock(mutex);
  checkpoint_locked();
}

void gc_flush_control::checkpoint()
{
  if (flushes.load(std::memory_order_relaxed) == nullptr)
    return;
  force_checkpoint();
}

class gc_cpu
{
private:
  std::atomic<gc_object *> pending;
  gc_object *last_observed[MAX_CPUS]; // XXX
  friend gc_object;

  gc_object * pop_ready();
public:
  gc_cpu() : pending(nullptr), last_observed {} { }
  void service();
  gc_object *observe(int cpu, gc_object *unless);
  void checkpoint(int cpu);
};                              // XXX - cache aligned

static gc_cpu cpus[MAX_CPUS];
static gc_flush_control flushes;

gc_object *
gc_cpu::pop_ready()
{
  cpu_mask_t seen = 0;
  std::atomic<gc_object*> *at = &pending;
  while (true) {
    gc_object *t = *at;
    if (t == nullptr)
      return nullptr;
    seen |= t->seen;
    if (cpu_seen_all(seen)) {
      *at = nullptr;
      return t;
    }
    at = &t->next;
  }
}

void
gc_cpu::service()
{
  gc_object *ready = pop_ready();
  if (ready == nullptr)
    return;

  while (ready) {
    gc_object *next = ready->next;
    assert(ready->dispatched == false);
    ready->dispatched = true;
    delete ready;
    ready = next;
  }
}

void
gc_object::gc_free()
{
  assert(scheduled == false);
  scheduled = true;
  gc_cpu &mycpu = cpus[cpu_id()];
  gc_object *sub = mycpu.pending;
  set_next(sub);
  mycpu.pending = this;
}

void
gc_object::observe(int cpu)
{
  seen |= (1 << cpu);
}

// XXX - This is broken.  unless might have been recycled, in which
// case we would want to observe it.
gc_object *
gc_cpu::observe(int cpu, gc_object *unless)
{
  gc_object *head = pending;
  if (head != nullptr && head != unless)
    head->observe(cpu);
  return head;
}

// XXX - gc_cpu has to be told its id? 
void
gc_cpu::checkpoint(int cpu)
{
  // XXX -why not cpu_count()?
  for (int i = 0; i < MAX_CPUS; ++i)
    last_observed[i] = cpus[i].observe(cpu, last_observed[i]);
  service();
}

void
gc_checkpoint()
{
  cpus[cpu_id()].checkpoint(cpu_id());
  flushes.checkpoint();
}

void
gc_flush()
{
  flushes.flush();
}

void
gc_finish()
{
  for (int i = 0; i < cpu_count(); ++i)
    cpus[i].checkpoint(i);
  for (int i = 0; i < cpu_count(); ++i)
    cpus[i].checkpoint(i);
}

void
gc_exit()
{
  gc_checkpoint();
  cpu_exit();
  gc_checkpoint();
  flushes.force_checkpoint();
}

static thread_local int gc_thread_locked = 0;

void gc_lock()
{
  gc_thread_locked++;
}

void gc_unlock()
{
  if (--gc_thread_locked == 0) {
    gc_checkpoint();
  }
}
