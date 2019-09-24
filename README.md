# atomic_mutex
A shared lock, upgradable to a unique lock, and a corresponding atomic-operation-based mutex.

This small project was an attempt to create a lock and mutex that:
- Provide both shared and unique locking modes (e.g. supporting multi-reader single-writer data protection).
- Provide lock upgrade capability in a single atomic operation (e.g. a shared lock can be upgraded to a unique lock).
- Are faster than std::mutexes, by essentially wrapping a ```std::atomic<int>```. All locking operations are performed using the int.
- Are more or less compatible with std locks (e.g. the mutex be used with std::shared_lock or std::unique_lock if so desired).

## Example usage:
Usage of shared_atomic_mutex with upgradable_lock is straightforward:
```cpp
shared_atomic_mutex m;
void read_data()
{
  upgradable_lock lock(m);
  // shared lock is acquired, all other threads may acquire shared locks but not unique locks
  // [read from cache]
  if (/*need to write to cache*/)
  {
    lock.upgrade();
    // unique lock is now acquired, all other threads are blocked from acquiring shared or unique locks
    // [write to cache]
  }
} // the shared or unique lock is released as appropriate
```

The mutex was benchmarked to be approx. 50% faster than a simple std::mutex and std::lock_guard, and 30% faster than a pair of std::mutexes and upgradable lock.

## What's Included?
This repo was created in Visual Studio 2019. The atomic mutex and its lock header-only and are found in /shared_atomic_mutex:
- shared_atomic_mutex.h
- upgradable_lock.h

The /shared_atomic_mutex_tests folder contains unit tests for the mutex and lock.

