jimcached is a memcached clone written in C++ using lock-free
algorithms. This file contains notes for anyone interested in
understanding the source code or hacking on it -- most likely me in
the future. :)

Components
==========

This is a short description of the major components of the system,
high to low-level.

TCP Server and IO Scheduler (tcp.cc, pool.cc)
--------------------------------------------

jimcached is built with Boost's ASIO framework. I looked at other
event frameworks (libevent and libuv) but I chose ASIO mostly because
it's very C++. The thread parallelism of the jimcached server is fixed
at start-up. We use a separate ASIO scheduler for each thread. Each
new TCP connection is assigned to a scheduler, round-robin and all
events for that connection are handled by that scheduler (thread).

Session (session.cc)
--------------------

The session is an infinite loop: reading incoming commands, executing
them and emitting the results. Most commands are interacting with the
cache, so most of the session code is marshaling data for cache
operations.

To simplify the IO code a bit all responses, besides the data from the
GET command, are staged in a small output buffer which is flushed at
the end of every command.

Error handling feels weird and I'm not settled about it. Exceptions
don't play well with asynchronous callbacks, so I mostly avoided using
them. Instead, I use `std::error_code` to report error states, even
internal cache errors. I'm not sure that this is a recommended use of
`std::error_code` but it works okay since there are so few errors
anyway. There is some messiness because Boost ASIO is still using the
Boost version of `error_code`. I had to add a little adapter.

Cache (cache.cc, entry.cc)
--------------------------

The cache interface is basically a functional version of the memcached
protocol. `cache.cc`'s translates these operations into operations on
the underlying hash-table.  Additionally it contains functions for
growing the size of the table.  This process actually introduces most
of the complexity in `cache.cc`, which would otherwise be a thin
wrapper around the table.

Periodically the user of the cache should call the `collect()`
function. This creates a new table and copies all "live" entries from
the old table to the new. This process may be initiated asynchronously
from ongoing cache operations. While collect is running, cache
operations must obey special rules regarding how they mutate the table.

When `collect()` is building the new table, it shares key and value
references with the original table, so they do not need to be copied.

Open Hash Table (table.h)
-------------------------

This is the heart of the cache. It's a basic open-hash table using
secondary hashes as its probing strategy.  Keys can never be deleted
from the table once they are added, but values can be.

There is support for "sharing" values with another table instance,
which is used by `cache.cc` to grow the table or evict expired
entries.

Operations are implemented using atomic operations. No backoff or
fallback mechanism is implemented in the compare-and-swap loops. It
remains to be seen if that will be a problem.

Removed values are not deleted directly, but released for garbage collection.

Garbage Collection (gc.cc)
--------------------------

