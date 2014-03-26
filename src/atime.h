#include <time.h>
#include <atomic>

class timestamp
{
private:
  std::atomic<time_t> t_;
public:
  static time_t now() { return time(NULL); }
  timestamp() : t_(now()) { }
  void update() { t_ = now(); }
  operator time_t () const { return t_.load(); }
};
