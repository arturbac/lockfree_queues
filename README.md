- [lockfree_queues](#lockfree_queues)
- [Status](#Status)

# lockfree_queues

lockfree lifo, fifo, aggregated pull fifo generic Modern C++ (actualy c++17) queues.

- Header only
- .cc code only for unit tests

code is initialy tested/build with clang 7/8
in case of interest I'm open to port/check with gcc and lower requirements to c++14

# Status
- stack, afifo are initialy finished and pass high contention tests in the wild or udner ASAN, have plans to extend tests
- fifo_queue hs still problem with delayed reclamation
