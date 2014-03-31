#include <ostream>

/* A silly logging class. */

struct log_level
{
  const char *name_;
  int verbosity_;
};

extern int verbosity;
constexpr log_level ERROR = { "ERROR", 1 };
constexpr log_level WARN = { "WARNING", 1 };
constexpr log_level INFO = { "INFO", 2 };
constexpr log_level DEBUG = { "DEBUG", 3 };
extern std::ostream null_log;

static inline std::ostream&
operator<<(std::ostream& out, const log_level& level)
{
  if (verbosity >= level.verbosity_)
    return out << level.name_ << " ";
  else
    return null_log;
}
