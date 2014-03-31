#include <time.h>
#include <thread>

class service
{
private:
  cache &c;
  std::ostream &log;
  std::atomic<bool> running;
  std::thread worker;
  static const clockid_t clock = CLOCK_MONOTONIC;
  static const int service_period_sec = 5;
  typedef struct timespec time_type;
  static time_type next();
  void loop();
  void run();
  void entry();
public:
  service(cache &c, std::ostream &log);
  ~service();
};
