/* An open hash table. */
#include <atomic>
#include <functional>
#include <cassert>

#include "counter.h"

//static constexpr int
//fast_log2(size_t s)
//{
//  return sizeof(s) * 8 - 1 - __builtin_clzl(s);
//}

typedef unsigned __int128 hash_t; // XXX - member of opentable?

template <class KT, class VT, class KR=const KT&>
class opentable : public gc_object
{
public:
  typedef flagged_ptr<VT> value_ref;

private:
  enum { shared_flag  = 1 };

  // XXX - normalize to std::function, or more template shit?
  typedef bool (*eq_f)(KR, KR);
  typedef hash_t (*hash_f)(KR, int seed);
  typedef void (*key_release_f)(KT *);
  typedef std::function<void (VT *)> val_release_f;

  const int lg2size_;
  const eq_f eq;
  const hash_f hash;
  const key_release_f key_release;
  const val_release_f val_release;
  static constexpr int probes = 16;
  static constexpr int probe_block_lg2 = 6; // cache line size;

  counter value_count; // number of values
  counter usage_count; // usage of keys

  class bucket_t {
  public:
    std::atomic<KT *> k;
    std::atomic<value_ref> v;
    bucket_t() : k(nullptr), v(nullptr) { }
    ~bucket_t() {
      KT *kk = k.load(std::memory_order_relaxed);
      VT *vv = v.load(std::memory_order_relaxed).get_ptr();
      if (kk)
        delete kk;
      if (vv)
        delete vv;
    }
  };

  //enum { entry_size_lg2 = fast_log2(sizeof(bucket_t)); }
  bucket_t *table;

  // Helper functions:
  size_t mask() const { return size() - 1; }

  // Find bucket containing the key, or candidates that could contain
  // the key and call find_f.  Iteration stops when find_f returns true
  // or there are no more possible entries.
  template<class F>
  bool iterate_buckets(KR key, F action);

  // Allocate a bucket for the given key. If the key already exists,
  // returns the existing bucket and frees the key, if it is not
  // shared.
  bucket_t *allocate_bucket(KT *key, KT **cur_key);

  // Find a bucket, if it exists, for the given key.
  bucket_t *find_bucket(KR key);

  // Set the key of the bucket, returns nullptr on failure, *b.k on success
  KT *set_key(bucket_t &b, KT *key);

  // Called when a bucket value has been changed
  void changed_value(value_ref old);

  value_ref value_tail(value_ref from, value_ref end);
  value_ref current_value(value_ref base);

  KT * set_impl(KT *key, value_ref value) noexcept;
  bool add_impl(KT *key, value_ref value,
                KT **cur_key, VT **cur_value) noexcept;

  void set_value(bucket_t &b, value_ref value);
  bool replace_value(bucket_t &b, value_ref value);
  bool add_value(bucket_t &b, value_ref value, VT **cur_value);
  bool remove_value(bucket_t &b);

  opentable(const opentable&) = delete;

public:
  opentable(int lg2size, eq_f eq, hash_f hash,
            key_release_f key_release, val_release_f val_release);

  ~opentable();

  // Find the requested key, or nullptr if it doesn't exist.
  VT *find(KR key) noexcept;
  // Set the given key to the given value. Replaces existing values,
  // Returns true on success.
  KT *set(KT *key, VT *value) noexcept;
  // Set key to value, unless key exists.
  bool add(KT *key, VT *value, KT **cur_key, VT **cur_value) noexcept;
  // Replace existing entry with new value, returns true on success
  bool replace(KR key, VT *value) noexcept;
  // Remove key from table, returns true if key was present
  bool remove(KR key) noexcept;

  int lg2size() const { return lg2size_; }
  size_t size() const { return 1ULL << lg2size_; }
  size_t usage() const { return usage_count; }

  KT *set_shared(KT *key, VT *value) noexcept;
  bool add_shared(KT *key, VT *value, KT **cur_key, VT **cur_value) noexcept;

  void exclusive(KT *key, VT *value) noexcept;

  class bucket_ref
  {
  private:
    bucket_t &b;
  public:
    bucket_ref(bucket_t &b) : b(b) { }
    void reset();
    KT *key() const { return b.k.load(); }
    VT *value() const { return b.v.load().get_ptr(); }
  };

  class iterator
  {
    bucket_t *ref;
    bucket_t *const end;
    void advance();
  public:
    iterator(bucket_t *ref, bucket_t *end)
      : ref(ref), end(end) { advance(); }
    bucket_ref operator*() const { return *ref; }
    iterator & operator++() { ref++; advance(); return *this; }
    bool operator==(const iterator &a) const { return ref == a.ref; }
    bool operator!=(const iterator &a) const { return ref != a.ref; }
  };
  iterator begin() { return iterator(table, table + size()); }
  iterator end() { return iterator(table + size(), table + size()); }

  class const_iterator
  {
    const bucket_t *ref;
    const bucket_t *const end;
    KT *k;
    void advance();
  public:
    const_iterator(bucket_t *ref, bucket_t *end)
      : ref(ref), end(end), k(nullptr) { advance(); }
    std::pair<KT *, VT*> operator*() const
    { return std::make_pair(k, ref->v.load().get_ptr()); }
    const_iterator & operator++() { ref++; advance(); return *this; }
    bool operator==(const const_iterator &a) const { return ref == a.ref; }
    bool operator!=(const const_iterator &a) const { return ref != a.ref; }
  };
  const_iterator cbegin() const {
    return const_iterator(table, table + size());
  }
  const_iterator cend() const {
    return const_iterator(table + size(), table + size());
  }

};

template<class KT, class VT, class KR>
opentable<KT,VT,KR>::opentable(int lg2size, eq_f eq, hash_f hash,
                               key_release_f key_release,
                               val_release_f val_release)
  : lg2size_(lg2size), eq(eq), hash(hash), key_release(key_release),
    val_release(val_release), value_count(0), usage_count(0) {
  table = new bucket_t[size()];
}

template<class KT, class VT, class KR>
VT *opentable<KT, VT, KR>::find(KR key) noexcept
{
  const bucket_t *b = find_bucket(key);
  if (b) {
    return b->v.load().get_ptr();
  } else {
    return nullptr;
  }
}

// Take exclusive ownership of the given key/value (which were
// possibly add/set_shared). If these are not present in the table,
// then free them.
//
// k must be valid, but v may be nullptr
template<class KT, class VT, class KR>
void opentable<KT, VT, KR>::exclusive(KT *k, VT *v) noexcept
{
  bucket_t *b = find_bucket(*k);
  if (b == nullptr) {
    key_release(k);
    val_release(v);
    return;
  }

  if (b->k != k)
    key_release(k);

  if (v == nullptr)
    return;

  value_ref expected = value_ref(v, shared_flag);
  if (!b->v.compare_exchange_strong(expected, v))
    val_release(v);
}

template<class KT, class VT, class KR>
opentable<KT, VT, KR>::~opentable()
{
  delete[] table;
}

template<class KT, class VT, class KR>
KT *opentable<KT, VT, KR>::set(KT *key, VT *value) noexcept
{
  return set_impl(key, value);
}

template<class KT, class VT, class KR>
KT *opentable<KT, VT, KR>::set_shared(KT *key, VT *value) noexcept
{
  return set_impl(key, value_ref(value, shared_flag));
}

template<class KT, class VT, class KR>
bool opentable<KT, VT, KR>::add(KT *key, VT *value,
                            KT **cur_key, VT **cur_value) noexcept
{
  return add_impl(key, value, cur_key, cur_value);
}

template<class KT, class VT, class KR>
bool opentable<KT, VT, KR>::add_shared(KT *key, VT *value,
                                   KT **cur_key, VT **cur_value) noexcept
{
  return add_impl(key, value_ref(value, shared_flag), cur_key, cur_value);
}


template<class KT, class VT, class KR>
auto opentable<KT, VT, KR>::allocate_bucket(KT *key, KT **cur_key) -> bucket_t *
{
  bucket_t *found = nullptr;
  iterate_buckets(*key, [&](bucket_t& b) {
      KT *k = set_key(b, key);
      if (k == nullptr)
        return false;
      found = &b;
      if (cur_key)
        *cur_key = k;
      return true;
    });
  return found;
}

template<class KT, class VT, class KR>
auto opentable<KT, VT, KR>::find_bucket(KR key) -> bucket_t *
{
  bucket_t *found = nullptr;
  iterate_buckets(key, [&](bucket_t &b) {
      KT *cur = b.k.load();
      if (cur == nullptr) {
        return true;
      } else if (eq(*cur, key)) {
        found = &b;
        return true;
      } else {
        return false;
      }
    });
  return found;
}

template<class KT, class VT, class KR>
KT *opentable<KT, VT, KR>::set_impl(KT *key, value_ref value) noexcept
{
  KT *cur_key;
  bucket_t *b = allocate_bucket(key, &cur_key);
  if (b != nullptr) {
    set_value(*b, value);
    return cur_key;
  } else {
    return nullptr;
  }
}

template<class KT, class VT, class KR>
bool opentable<KT, VT, KR>::add_impl(KT *key, value_ref value,
                                 KT **cur_key, VT **cur_value) noexcept
{
  bucket_t *b = allocate_bucket(key, cur_key);
  if (b != nullptr) {
    return add_value(*b, value, cur_value);
  } else {
    if (cur_key)
      *cur_key = nullptr;
    if (cur_value)
      *cur_value = nullptr;
    return false;
  }
}

template <class KT, class VT, class KR>
bool opentable<KT, VT, KR>::replace(KR key, VT *value) noexcept
{
  bucket_t *b = find_bucket(key);
  if (b == nullptr)
    return false;

  return replace_value(*b, value);
}

template <class KT, class VT, class KR>
bool opentable<KT, VT, KR>::remove_value(bucket_t &b)
{
  value_ref old = b.v.exchange(nullptr);
  if (old == nullptr)
    return false;

  value_count.decr();
  if (!old.get_flag(shared_flag))
    val_release(old.get_ptr());
  return true;
}

template <class KT, class VT, class KR>
bool opentable<KT, VT, KR>::remove(KR key) noexcept
{
  bucket_t *b = find_bucket(key);
  if (b == nullptr)
    return false;

  return remove_value(*b);
}

/* Iterate over buckets eligible for holding the given key.  Call
 * action for each empty or matching bucket until action returns true.
 */
template<class KT, class VT, class KR>
template<class F>
bool opentable<KT, VT, KR>::iterate_buckets(KR key, F action)
{
  // XXX - is this really what we want?
  int seed = 0;
  hash_t h = hash(key, seed++);
  int bits = sizeof(h) * 8;
  size_t i = 0;
  for (int j = 0; j < size(); ++j) {
    if (bits < lg2size_) {
      h = hash(key, seed++);
      bits = sizeof(h) * 8;
    }
    i = (i + h) & mask();
    KT *cur = table[i].k.load();
    if (cur == nullptr || eq(key, *cur))
      if (action(table[i]))
        return true;
    h >>= lg2size_;
    bits -= lg2size_;
  }
  return false;
}

template<class KT, class VT, class KR>
KT *opentable<KT,VT,KR>::set_key(bucket_t &b, KT *key)
{
  KT *cur = b.k.load();
  while (cur == nullptr) {
    if (b.k.compare_exchange_weak(cur, key)) {
      usage_count.incr();
      return key;
    }
  }
  if (eq(*cur, *key)) {
    return cur;
  } else {
    return nullptr;
  }
}

template<class KT, class VT, class KR>
void opentable<KT,VT,KR>::changed_value(value_ref old)
{
  if (old == nullptr) {
    value_count.incr();
  } else if (!old.get_flag(shared_flag)) {
    val_release(old.get_ptr());
  }
}

template<class KT, class VT, class KR>
void opentable<KT,VT,KR>::set_value(bucket_t &b, value_ref value)
{
  value_ref previous = b.v.exchange(value);
  changed_value(previous);
}

template<class KT, class VT, class KR>
bool opentable<KT,VT,KR>::replace_value(bucket_t &b, value_ref value)
{
  value_ref previous = b.v.load();
  do {
    // XXX - backoff?
    if (previous == nullptr)
      return false;
  } while (!b.v.compare_exchange_weak(previous, value));
  changed_value(previous);
  return true;
}

template<class KT, class VT, class KR>
bool opentable<KT,VT,KR>::add_value(bucket_t &b, value_ref value, VT **cur_value)
{
  value_ref previous = nullptr;
  if (b.v.compare_exchange_strong(previous, value)) {
    changed_value(previous);
    if (cur_value)
      *cur_value = value.get_ptr();
    return true;
  } else {
    if (cur_value)
      *cur_value = previous.get_ptr();
    return false;
  }
}

template<class KT, class VT, class KR>
void opentable<KT, VT, KR>::const_iterator::advance()
{
  for (; ref != end; ++ref) {
    k = ref->k;
    if (k != nullptr)
      return;
  }
  k = nullptr;
}

template<class KT, class VT, class KR>
void opentable<KT, VT, KR>::iterator::advance()
{
  for (; ref != end; ++ref) {
    KT *k = ref->k;
    if (k != nullptr)
      return;
  }
}

template<class KT, class VT, class KR>
void opentable<KT, VT, KR>::bucket_ref::reset()
{
  b.k.store(nullptr); //XXX - , std::memory_order_relaxed);
  b.v.store(nullptr); //XXX - , std::memory_order_relaxed);
}
