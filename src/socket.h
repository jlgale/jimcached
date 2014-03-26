/* -*-c++-*- */

#include <cerrno>
#include <iostream>
#include <unistd.h>

class Socket
{
  int s;
  int _recv(buffer &b, int flags) const;
public:
  Socket();
  Socket(int a) : s(a) { }
  ~Socket() { close(s); }
  void reuse() const;
  void nodelay() const;
  void bind(char * iface, int port) const;
  void listen(int backlog) const;
  Socket accept() const;
  int recv(buffer &b, int flags) const;
  int recv_r(buffer &b, int flags) const;
  int send_r(buffer &b) const;
};

class socket_error
{
  int err;
  const char *msg;
public:
  socket_error(const char *msg = NULL) : err(errno), msg(msg) { }
  friend std::ostream& operator<<(std::ostream&, const socket_error&);
};

inline std::ostream&
operator<<(std::ostream& o, const socket_error& se)
{
  return o << se.msg << ": " << strerror(se.err);
}

class socket_closed
{
};
