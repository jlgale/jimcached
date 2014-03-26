
template <class T>
class mv_object {
  enum { del_flag = 1 };
  std::atomic<flagged_ptr<T> > newer_;
  
  typedef flagged_ptr<T> newer_t;
  T *tail(T *end);
 public:
  mv_object() : newer_(nullptr) { }
  virtual ~mv_object();
  void mv_set(T *e);
  bool mv_add(T *e);
  bool mv_replace(T *e);
  bool mv_del();
  T *newest();
  T *newer();
};

template <class T>
mv_object<T>::~mv_object()
{
  newer_t n = newer_.load();
  if (n != nullptr)
    delete n.get_ptr();
}

template <class T>
T *mv_object<T>::tail(T *end)
{
  newer_t nxt = newer_.load();
  if (nxt.get_ptr() == end) {
    if (nxt.get_flag(del_flag))
      return nullptr;
    else
      return static_cast<T*>(this); // XXX
  }
  return nxt.get_ptr()->tail(end);
}

template <class T>
T *mv_object<T>::newest()
{
  return tail(nullptr);
}

template <class T>
T * mv_object<T>::newer()
{
  return newer_.load().get_ptr();
}

template <class T>
void mv_object<T>::mv_set(T *e)
{
  newer_t expected = nullptr;
  while (!newer_.compare_exchange_weak(expected, e)) {
    if (expected != nullptr)
      return expected.get_ptr()->mv_set(e);
    expected = newer_t(nullptr, expected.get_flags());
  }
}

template <class T>
bool mv_object<T>::mv_add(T *e)
{
  newer_t expected = newer_t(nullptr, del_flag);
  while (!newer_.compare_exchange_weak(expected, e)) {
    if (expected != nullptr) {
      return expected.get_ptr()->mv_add(e);
    } else if (!expected.get_flag(del_flag)) {
      return false;
    }
  }
  return true;
}

template <class T>
bool mv_object<T>::mv_replace(T *e)
{
  newer_t expected = nullptr;
  while (!newer_.compare_exchange_weak(expected, e)) {
    if (expected == nullptr && expected.get_flag(del_flag)) {
      return false;
    } else if (expected != nullptr) {
      return expected.get_ptr()->mv_replace(e);
    }
  }
  return true;
}

template <class T>
bool mv_object<T>::mv_del()
{
  newer_t expected = nullptr;
  while (!newer_.compare_exchange_weak(expected, newer_t(nullptr, del_flag))) {
    if (expected == nullptr && expected.get_flag(del_flag)) {
      return false;
    } else if (expected != nullptr) { // current is not nullptr
      return expected.get_ptr()->mv_del();
    }
    expected = nullptr;
  }
  return true;
}
