#include "buffer.h"
#include "cache.h"
#include "murmur2.h"

#include <limits>
#include <algorithm>

cache_key::cache_key(char *b, buf src) : buf(b, src.size())
{
  memcpy(b, src.headp(), src.size());
}

// Warning - placement new is being used here with vanilla
// deletes. ie we are assuming delete just calls free().
cache_key *
cache_key::alloc(buf src)
{
  char *b = (char *)malloc(sizeof(cache_key) + src.size());
  return new (b) cache_key(b + sizeof(cache_key), src);
}

static bool
key_eq(buf a, buf b)
{
  if (a.size() == b.size())
    return memcmp(a.headp(), b.headp(), a.size()) == 0;
  return false;
}

static hash_t
key_hash(buf a, int seed)
{
  return MurmurHash64A(a.headp(), a.size(), seed);
}

static void
key_release(cache::key *k)
{
  k->gc_free();
}

void
cache::entry_release(entry *e)
{
  size_t size = 0;
  // XXX - seems wrong that we have to walk this
  for (entry *x = e; x; x = x->newer())
    size += x->size();
  bytes_.sub(size);
  e->gc_free();
}

auto cache::new_table(int lg2size) -> table_t *
{
  return new opentable<key, entry, buf>(lg2size, key_eq, key_hash,
                                            key_release,
                                            std::bind<void>(&cache::entry_release, this,
                                                            std::placeholders::_1));
}

cache::cache(size_t max_bytes) : max_bytes(max_bytes), flushed(0),
                                 _entries(new_table(initial_lg2size)),
                                 _building(nullptr) { }

cache_error_t
cache::set(buf k, unsigned flags, unsigned exptime, const rope &r)
{
  sets_.incr();
  std::unique_ptr<key> mykey(key::alloc(k));
  std::unique_ptr<entry> e(new entry(flags, exptime, r));
  key *cur_key;
  table_t *entries, *building;
  if (is_building(&entries, &building)) {
    entry *cur_entry;
    if (entries->add(mykey.get(), e.get(), &cur_key, &cur_entry)) {
      building->set_shared(cur_key, cur_entry);
    } else if (cur_entry) {
      cur_entry->mv_set(e.get());
    } else {
      // XXX - can this happen?
      assert(cur_key == nullptr);
    }
  } else {
    cur_key = entries->set(mykey.get(), e.get());
  }

  if (mykey.get() == cur_key)
    mykey.release();
  if (cur_key == nullptr)
    return cache_error_t::set_error;

  bytes_.add(r.size());
  e.release();
  return cache_error_t::stored;
}

cache_error_t
cache::add(buf k, unsigned flags, unsigned exptime, const rope &r)
{
  sets_.incr();
  std::unique_ptr<key> mykey(key::alloc(k));
  std::unique_ptr<entry> e(new entry(flags, exptime, r));

  key *cur_key;
  table_t *entries, *building;
  bool success;
  if (is_building(&entries, &building)) {
    entry *cur_entry;
    success = entries->add(mykey.get(), e.get(), &cur_key, &cur_entry);
    if (success) {
      building->add_shared(cur_key, cur_entry, NULL, NULL);
    } else {
      success = cur_entry->mv_add(e.get());
    }
  } else {
    success = entries->add(mykey.get(), e.get(), &cur_key, NULL);
  }

  if (mykey.get() == cur_key)
    mykey.release();

  if (!success)
    return cache_error_t::set_error;

  bytes_.add(r.size());
  e.release();
  return cache_error_t::stored;
}

cache_error_t
cache::replace(buf k, unsigned flags,
               unsigned exptime, const rope &r)
{
  sets_.incr();
  std::unique_ptr<entry> e(new entry(flags, exptime, r));
  table_t *entries;
  if (is_building(&entries, NULL)) {
    entry *cur = entries->find(k);
    if (!cur || !cur->mv_replace(e.get()))
      return cache_error_t::set_error;
  } else {
    if (!entries->replace(k, e.get()))
      return cache_error_t::set_error;
  }
  bytes_.add(r.size());
  e.release();
  return cache_error_t::stored;
}

cache::ref
cache::get(buf k)
{
  gets_.incr();
  entry *e = _entries.load()->find(k);
  if (e) {
    return e->newest();
  } else {
    get_misses_.incr();
    return nullptr;
  }
}

cache_error_t
cache::del(buf k)
{
  table_t *entries;
  if (is_building(&entries, NULL)) {
    entry *cur = entries->find(k);
    if (!cur || !cur->mv_del())
      return cache_error_t::notfound;
  } else {
    if (!entries->remove(k))
      return cache_error_t::notfound;
  }
  return cache_error_t::deleted;
}

cache_error_t
cache::append(buf key, const rope &suffix)
{
  ref e = _entries.load()->find(key);
  if (e == nullptr)
    return cache_error_t::set_error;
  bytes_.add(suffix.size());
  e->append(suffix);
  return cache_error_t::stored;
}

cache_error_t
cache::prepend(buf key, const rope &prefix)
{
  ref e = get(key);
  if (e == nullptr)
    return cache_error_t::set_error;
  bytes_.add(prefix.size());
  e->prepend(prefix);
  return cache_error_t::stored;
}

cache_error_t
cache::incr(buf k, uint64_t v, uint64_t *vout)
{
  ref e = get(k);
  if (e == nullptr)
    return cache_error_t::set_error;
  *vout = e->incr(v);
  return cache_error_t::stored;
}

cache_error_t
cache::decr(buf k, uint64_t v, uint64_t *vout)
{
  ref e = get(k);
  if (e == nullptr)
    return cache_error_t::set_error;
  *vout = e->decr(v);
  return cache_error_t::stored;
}

cache_error_t
cache::cas(buf k, uint32_t flags, uint32_t exptime,
           uint64_t ver, const rope &r)
{
  ref e = get(k);
  if (e == nullptr)
    return cache_error_t::notfound;
  // XXX - update bytes
  if (!e->cas(flags, exptime, ver, r))
    return cache_error_t::cas_exists;
  return cache_error_t::stored;
}

cache_error_t
cache::touch(buf k, unsigned exptime)
{
  touches_.incr();
  ref e = get(k);
  if (e == nullptr)
    return cache_error_t::notfound;
  e->touch(exptime);
  return cache_error_t::stored;
}

time_t
cache::get_atime_cutoff(const table_t &t) const
{
  const double p = (max_bytes * (1.0 - reserve_percentage)) / bytes_;
  if (p >= 1.0)
    return 0;

  time_t sample[sample_size];
  int j = 0;
  for (table_t::const_iterator i = t.cbegin();
       i != t.cend() && j < sample_size; ++i) {
    auto pair = *i;
    entry *c = pair.second;
    sample[j++] = std::max(c->get_atime(), c->get_mtime());
  }

  int k = j * (1.0 - p);
  assert(k >= 0);
  assert(k < j);
  std::nth_element(sample, sample + k, sample + j);

  return sample[k];
}

bool
cache::entry_is_live(entry &e, const time_t &cutoff,
                     const time_t &now) const
{
  const entry &c = *e.newest();
  time_t mtime = c.get_mtime();
  if (mtime < flushed) {
    return false;
  } else if (mtime < cutoff && c.get_atime() < cutoff) {
    return false;
  } else if (c.get_exptime() != 0 && c.get_exptime() < now) {
    return false;
  }
  return true;
}

void
cache::collect()
{
  table_t *old = _entries.load();
  int new_lg2size = old->lg2size();
  if (old->usage() >= old->size() * usage_grow_threshold)
    new_lg2size++;
  table_t *building = new_table(new_lg2size);
  _building = building;
  gc_flush();

  // everyone now should see building
  time_t now = timestamp::now();
  time_t cutoff = get_atime_cutoff(*old);
  for (table_t::const_iterator i = old->cbegin(); i != old->cend(); ++i) {
    auto pair = *i;
    entry *c = pair.second;
    if (c && entry_is_live(*c, cutoff, now))
      building->add_shared(pair.first, pair.second, NULL, NULL);
  }
  _entries = building;
  _building = nullptr;
  gc_flush();
  // everyone now should see building is nullptr, not be using old
  // XXX - we could be more efficient here
  for (table_t::iterator i = old->begin(); i != old->end(); ++i) {
    table_t::bucket_ref b = *i;
    building->exclusive(b.key(), b.value());
    b.reset();
  }
  delete old;
  // compress....
}

bool cache::is_building(table_t **entries, table_t **building)
{
  table_t *e = _entries.load();
  table_t *b = _building.load();
  if (entries)
    *entries = e;
  if (building)
    *building = b;
  return b && b != e;
}

size_t cache::bytes() const
{
  return bytes_;
}

size_t cache::set_count() const
{
  return sets_;
}

size_t cache::get_count() const
{
  return gets_;
}

size_t cache::touch_count() const
{
  return touches_;
}

size_t cache::flush_count() const
{
  return flushes_;
}

size_t cache::get_miss_count() const
{
  return get_misses_;
}

size_t cache::get_hit_count() const
{
  size_t misses = get_misses_;
  size_t gets = gets_;
  return gets > misses ? gets - misses : 0;
}

size_t cache::buckets() const
{
  return _entries.load()->size();
}

size_t cache::keys() const
{
  return _entries.load()->usage();
}

void cache::flush_all(int delay)
{
  flushes_.incr();
  flushed = timestamp::now() + delay;
}

