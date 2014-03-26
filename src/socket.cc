#include <netinet/tcp.h>
#include <unistd.h>
#include <arpa/inet.h>

#include "buffer.h"
#include "socket.h"

Socket::Socket()
{
  s = socket(AF_INET, SOCK_STREAM, 0);
  if (s == -1)
    throw socket_error("Cannot create socket");
}

void
Socket::reuse() const
{
  int reuse = 1;
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) {
    throw socket_error("Cannot set SOREUSEADDR");
  }
}

void
Socket::bind(char *iface, int port) const
{
  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if (iface && inet_aton(iface, &addr.sin_addr))
    throw socket_error("Invalid socket interface");
  if (::bind(s, (struct sockaddr *)&addr, sizeof(addr)))
    throw socket_error("Could not bind socket");
}

void
Socket::listen(int backlog) const
{
  if (::listen(s, backlog))
    throw socket_error("Could not listen socket");
}

void
Socket::nodelay() const
{
  int nodelay = 1;
  if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)))
    throw socket_error("Cannot set TCP_NODELAY");
}

Socket
Socket::accept() const
{
  int a = ::accept(s, NULL, NULL);
  if (a == -1)
    throw socket_error("Accept error");
  return Socket(a);
}

int
Socket::_recv(buffer &b, int flags) const
{
  int r = ::recv(s, b.tailp(), b.available(), flags);
  if (r > 0)
    b.notify_write(r);
  return r;
}

int
Socket::recv(buffer &b, int flags) const
{
  int r = _recv(b, flags);
  if (r < 0)
    throw socket_error("recv error");
  if (r == 0)
    throw socket_closed();
  return r;
}

int
Socket::recv_r(buffer &b, int flags) const
{
  int r;
  do {
    r = _recv(b, flags);
  } while (r == -1 && (errno == EINTR || errno == EAGAIN));
  if (r < 0)
    throw socket_error("recv error");
  if (r == 0)
    throw socket_closed();
  return r;
}

int 
Socket::send_r(buffer &b) const
{
  int r;
  do {
    r = ::send(s, b.headp(), b.used(), 0);
  } while (r == -1 && (errno == EINTR || errno == EAGAIN));
  if (r < 0)
    throw socket_error("send error");
  return r;
}
