/* -*-c++-*- */

#include <map>
#include <memory>
#include <cstdint>

#include "mem.h"
#include "rope.h"
#include "entry.h"
#include "table.h"

enum class cache_error_t
{
  stored = 0,
  deleted,
  notfound,
  set_error,
  cas_exists,
};

class cache_key : public buffer, public gc_object
{
public:
  cache_key(buf src) : buffer(src) { }
};

class cache
{
public:
  typedef cache_key key;
  typedef entry *ref;
private:
  static constexpr int initial_lg2size = 20;
  // XXX - pick a real number, or parameterize
  static constexpr double usage_grow_threshold = 0.75; // cf. wikipedia
  static constexpr double reserve_percentage = 0.10;
  static constexpr int sample_size = 8192;
  const size_t max_bytes;
  time_t flushed;               // XXX - atomic

  typedef opentable<key, entry, buf> table_t;
  std::atomic<table_t *> _entries;
  std::atomic<table_t *> _building;

  table_t *new_table(int lg2size);
  void entry_release(entry *e);

  bool is_building(table_t **entries, table_t **building);
  time_t get_atime_cutoff(const table_t &t) const;
  // XXX - entry& should be const
  bool entry_is_live(entry &e, const time_t &cutoff, const time_t &now) const;

  counter bytes_;
  counter sets_;
  counter gets_;
  counter touches_;
  counter flushes_;
  counter get_misses_;

public:

  cache(size_t max_bytes);
  virtual ~cache() { delete _entries.load(); }
  ref get(buf k);
  cache_error_t set(buf k, unsigned flags,
                    unsigned exptime, const rope &r);
  cache_error_t add(buf k, unsigned flags,
                    unsigned exptime, const rope &r);
  cache_error_t replace(buf k, unsigned flags,
                        unsigned exptime, const rope &r);
  cache_error_t del(buf k);
  cache_error_t append(buf k, const rope &suffix);
  cache_error_t prepend(buf k, const rope &prefix);
  cache_error_t incr(buf k, uint64_t v, uint64_t *vout);
  cache_error_t decr(buf k, uint64_t v, uint64_t *vout);
  cache_error_t cas(buf k, uint32_t flags, uint32_t exptime,
                    uint64_t ver, const rope &r);
  cache_error_t touch(buf k, unsigned exptime);
  void flush_all(int delay);

  size_t bytes() const;
  size_t buckets() const;
  size_t keys() const;
  size_t get_count() const;
  size_t get_hit_count() const;
  size_t get_miss_count() const;
  size_t set_count() const;
  size_t touch_count() const;
  size_t flush_count() const;

  // Garbage collect old entries. Can be called concurrently with
  // other operations.
  void collect();
};
