template <class T>
class counter_base
{
private:
  std::atomic<T> value_;
public:
  counter_base(const T& value) : value_(value) { }
  counter_base() : counter_base(0) { }
  void incr() { value_++; }
  void decr() { value_--; }
  void add(const T &a) { value_ += a; }
  void sub(const T &a) { value_ -= a; }
  operator T () const { return value_.load(); }
};

typedef counter_base<ssize_t> counter;
