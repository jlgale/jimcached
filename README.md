jimcached -- a memcached clone
==============================

This is something the world probably doesn't need: yet another
memcached implementation. jimcached is intended to be a drop-in
replacement for memcached. It was developed by me to meet a couple of
personal goals:

1. Experiment with lock-free algorithms. The cache itself --
implemented as a resizable, open hash table -- is entirely lock-free.

2. Refresh my C++ skills. I made it a point to do everything the "C++
way." Exceptions, sub-classes, templates, iostreams, Boost; they're
all there. For every design decision I asked myself, "What would
Stroustrup do?" I made of point of using C++11, since I wanted to
learn more about the new language.

The current status is as follows:

* all commands described in the memcached protocol.txt file are
  completely implemented, with the exception of `slabs`, `verbosity`
  and `stats`. `stats` is partially implemented.

* Memory management is very crude. Maximum memory usage for values is
  enforced using an approximate LRU eviction policy. Facilities for
  tuning memory usage or inspecting through stats are largely absent.

* Only the TCP protocol is implemented, although adding the UDP
  protocol should be easy enough.

* The command-line interface is identical to memcached's, although
  many memcached options are not implemented.

* Many productization features, such as pid files, are absent.

Compiling
---------

Building is the usual `./configure ; make` business. This is my first
attempt at using `automake` and friends, so there may be some rough
edges. My primary development platform is 64-bit Linux so I can only
vouch for that.  At one point it built on OS-X, so it should be pretty
easy to port to that or a BSD platform. You will need a recent C++
compiler (one that supports C++11) and Boost.

Compatibility
-------------

Although intended to be a drop-in replacement for memcached, users may
notice some differences:

* Set operations can fail if the hash table becomes full. Eviction and
  growing the table is done asynchronously, so operations may begin to
  fail if that process is running behind. The hope is that if
  jimcached is well tuned, that will never be a problem.

* Expiration is handled asynchronously. That means that an expired
  object might be returned to a request if the service process has not
  come around to remove it from the table. The hope is that the
  service will be quick.
  
* Memory usage is higher, overall and peak. When the memory limit is
  exceeded, jimcached does not begin evicting objects immediately.
  Rather it waits for an asynchronous service process to discriminate
  which objects should be freed.

Cheers,
Jim
