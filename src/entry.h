#include <cstdint>
#include <cstdlib>
#include <atomic>
#include <functional>

#include "gc.h"
#include "const_rope.h"
#include "flagged_ptr.h"
#include "history.h"
#include "atime.h"

struct mem_pair
{
  std::atomic<mem *> head;
  std::atomic<mem *> tail;
  
  mem_pair(mem *head, mem *tail) : head(head), tail(tail) { }
};

class entry : public gc_object, public mv_object<entry>
{
  uint32_t flags;
  uint32_t exptime;
  mem_pair data __attribute__((aligned(sizeof(struct mem_pair))));
  timestamp atime;
  timestamp mtime;
  bool deleted;                 // XXX - for debugging

  uint64_t incrdecr(std::function<uint64_t (uint64_t)> doit);
  entry(const entry &);            // No copies
  entry & operator=(const entry&); // No assignment

 public:

  entry(uint32_t flags, uint32_t exptime, const rope &r)
    : flags(flags), exptime(exptime),
      data(r.head(), r.tail()), deleted(false) {}
  ~entry();
  void append(const rope &r);
  void prepend(const rope &r);
  bool cas(uint32_t flags, uint32_t exptime, uint64_t version, const rope &r);

  uint64_t incr(uint64_t v);
  uint64_t decr(uint64_t v);
  void touch(uint32_t exptime);
  uint32_t get_flags() const { return flags; }
  uint32_t get_exptime() const { return exptime; }
  time_t get_atime() const { return atime; }
  time_t get_mtime() const { return mtime; }
  const_rope read();            // Updates atime
  size_t size() const;
  bool expired() const;         // XXX - unused
};
