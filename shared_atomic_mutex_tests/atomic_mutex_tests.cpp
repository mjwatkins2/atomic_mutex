#include "CppUnitTest.h"

#include <shared_atomic_mutex/upgradable_lock.h>
#include <shared_atomic_mutex/shared_atomic_mutex.h>

#include <assert.h>
#include <atomic>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <thread>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace atomic_mutex;

namespace atomic_mutex_tests
{
	TEST_CLASS(atomic_mutex_tests)
	{
	private:

		const int num_threads = 12;

		// Blocks threads until a certain number of threads are created, and then releases them all at once.
		class thread_blocker
		{
		public:
			thread_blocker(int num_expected_threads)
				: m_count(num_expected_threads) {}

			/* Blocks the current thread until all expected threads are created.
			The current thread counts toward the number of expected threads. */
			void wait_and_count()
			{
				m_count--;
				if (m_count <= 0)
					m_cv.notify_all(); // release the threads!
				assert(m_count >= 0);
				
				wait();
			}

			/* Blocks the current thread until all expected threads are created.
			The current thread does not count toward the number of expected threads
			but waits nonetheless. */
			void wait()
			{
				std::unique_lock<std::mutex> lock(m_mutex);
				m_cv.wait(lock, [this] { return m_count == 0; });
			}

		private:
			std::condition_variable m_cv;
			std::mutex m_mutex;
			std::atomic<int> m_count;
		};


		// Allows templates to be used for interchanging std::unique_lock and upgradable_lock
		class upgradable_lock_unique_wrapper
		{
		public:
			upgradable_lock_unique_wrapper(shared_atomic_mutex& m)
				: m_lock(m, true) {}
		private:
			upgradable_lock m_lock;
		};


		// Tests shared locking, e.g. multiple simultaneous readers from a cache
		template<typename LOCK>
		void test_readers()
		{
			shared_atomic_mutex mutex;

			std::vector<std::thread> threads;
			thread_blocker blocker(num_threads);

			std::atomic<int> running_threads = 0;
			for (int i = 0; i < num_threads; i++)
			{
				threads.emplace_back([&]()
					{
						blocker.wait_and_count(); // wait for all threads to be constructed before continuing

						LOCK lock(mutex);
						running_threads++;
						std::this_thread::sleep_for(std::chrono::milliseconds(200)); // do "work"
						running_threads--;
					});
			}
			blocker.wait();

			std::this_thread::sleep_for(std::chrono::milliseconds(50)); // wait for shared locks to be acquired
			Assert::AreEqual(num_threads, (int)mutex.num_shared_locks());
			Assert::AreEqual(num_threads, (int)running_threads);
			Assert::AreEqual(0, (int)mutex.num_unique_locks());
			Assert::IsFalse(mutex.is_unique_locked());

			for (auto& th : threads)
				th.join();

			Assert::AreEqual(0, (int)running_threads);
			Assert::AreEqual(0, (int)mutex.num_shared_locks());
			Assert::AreEqual(0, (int)mutex.num_unique_locks());
			Assert::IsFalse(mutex.is_unique_locked());
		}


		// Tests unique locking, e.g. one unique writer blocking access of other writers to a cache
		template<typename LOCK>
		void test_writers()
		{
			shared_atomic_mutex mutex;

			std::vector<std::thread> threads;
			thread_blocker blocker(num_threads);

			std::atomic<int> running_threads = 0;
			std::atomic<int> assert_inside_thread = 0; // don't put test asserts in another thread, it causes crashes
			for (int i = 0; i < num_threads; i++)
			{
				threads.emplace_back([&]()
					{
						blocker.wait_and_count(); // wait for all threads to be constructed before continuing

						LOCK lock(mutex);
						running_threads++;
						if (running_threads > 1)
							assert_inside_thread++;
						if (!mutex.is_unique_locked())
							assert_inside_thread++;
						std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // do "work"
						if (!mutex.is_unique_locked())
							assert_inside_thread++;
						running_threads--;
					});
			}
			blocker.wait();

			Assert::AreEqual(0, (int)mutex.num_shared_locks());
			std::this_thread::sleep_for(std::chrono::milliseconds(900)); // wait for unique locks to be acquired
			Assert::AreEqual(num_threads, (int)mutex.num_unique_locks());
			Assert::AreEqual(1, (int)running_threads);

			for (auto& th : threads)
				th.join();

			Assert::AreEqual(0, assert_inside_thread.load());
			Assert::AreEqual(0, (int)running_threads);
			Assert::AreEqual(0, (int)mutex.num_shared_locks());
			Assert::AreEqual(0, (int)mutex.num_unique_locks());
			Assert::IsFalse(mutex.is_unique_locked());
		}


		// Test that unique locks block shared locks
		template<typename SHARED_LOCK, typename UNIQUE_LOCK>
		void test_writers_blocking_readers()
		{
			const int num_writers = 2;
			const int num_readers = num_threads - num_writers;

			shared_atomic_mutex mutex;

			std::vector<std::thread> threads;
			thread_blocker blocker(num_threads);

			std::atomic<int> running_threads = 0;

			// add writers
			std::atomic<int> num_unique_locks = 0;
			std::atomic<int> assert_inside_wthread = 0; // don't put asserts in another thread, it causes crashes
			for (int i = 0; i < num_writers; i++)
			{
				threads.emplace_back([&]()
					{
						blocker.wait_and_count(); // wait for all threads to be constructed before continuing

						//std::unique_lock<shared_atomic_mutex> lock(mutex);
						UNIQUE_LOCK lock(mutex);
						running_threads++;
						num_unique_locks++;
						if (num_unique_locks > 1)
							assert_inside_wthread++;

						if (!mutex.is_unique_locked())
							assert_inside_wthread++;
						std::this_thread::sleep_for(std::chrono::milliseconds(1000));
						if (!mutex.is_unique_locked())
							assert_inside_wthread++;

						num_unique_locks--;
						running_threads--;
					});
			}

			// add readers which should wait for both writers
			std::atomic<int> assert_inside_rthread = 0;
			for (int i = 0; i < num_readers; i++)
			{
				threads.emplace_back([&]()
					{
						blocker.wait_and_count(); // wait for all threads to be constructed before continuing
						std::this_thread::sleep_for(std::chrono::milliseconds(100)); // pause for unique locks to be acquired first

						// check we are still waiting on all the writers
						if ((int)mutex.num_unique_locks() != num_writers)
							assert_inside_rthread++;
						if (!mutex.is_unique_locked())
							assert_inside_rthread++;

						//std::shared_lock<shared_atomic_mutex> lock(mutex);
						SHARED_LOCK lock(mutex);
						running_threads++;

						// check writers are done now
						if ((int)mutex.num_unique_locks() != 0)
							assert_inside_rthread++;
						if (mutex.is_unique_locked())
							assert_inside_rthread++;

						std::this_thread::sleep_for(std::chrono::milliseconds(100));
						running_threads--;
					});
			}

			blocker.wait();
			std::this_thread::sleep_for(std::chrono::milliseconds(500)); // wait for locks to be acquired

			// all writers but only one writer lock								 
			Assert::AreEqual(num_writers, (int)mutex.num_unique_locks());
			Assert::AreEqual(1, (int)running_threads);
			Assert::IsTrue(mutex.is_unique_locked());

			for (int i = 0; i < num_writers; i++)
				threads[i].join();

			Assert::AreEqual(0, assert_inside_wthread.load());
			Assert::AreEqual(num_readers, (int)mutex.num_shared_locks());
			Assert::AreEqual(num_readers, (int)running_threads);
			Assert::AreEqual(0, (int)mutex.num_unique_locks());

			for (int i = num_writers; i < num_threads; i++)
				threads[i].join();

			Assert::AreEqual(0, assert_inside_rthread.load());
			Assert::AreEqual(0, (int)mutex.num_shared_locks());
			Assert::AreEqual(0, (int)mutex.num_unique_locks());
			Assert::IsFalse(mutex.is_unique_locked());
		}

		// Test that shared locks block unique locks
		template<typename SHARED_LOCK, typename UNIQUE_LOCK>
		void test_readers_blocking_writers()
		{
			int num_writers = 2;
			int num_readers = num_threads - num_writers;

			shared_atomic_mutex mutex;
			std::atomic<int> running_threads = 0;

			std::vector<std::thread> threads;
			thread_blocker blocker(num_threads);

			// add readers
			for (int i = 0; i < num_readers; i++)
			{
				threads.emplace_back([&]()
					{
						blocker.wait_and_count(); // wait for all threads to be constructed before continuing

						SHARED_LOCK lock(mutex);
						running_threads++;
						std::this_thread::sleep_for(std::chrono::milliseconds(1000));
						running_threads--;
					});
			}

			// add writers which should wait for all readers
			std::atomic<int> num_unique_locks = 0;
			std::atomic<int> assert_inside_wthread = 0;
			for (int i = 0; i < num_writers; i++)
			{
				threads.emplace_back([&]()
					{
						blocker.wait_and_count();
						std::this_thread::sleep_for(std::chrono::milliseconds(100)); // wait for reader locks to be acquired first

						UNIQUE_LOCK lock(mutex);
						running_threads++;
						num_unique_locks++;
						if (num_unique_locks > 1)
							assert_inside_wthread++;

						if ((int)mutex.num_shared_locks() != 0) // all readers should be done
							assert_inside_wthread++;
						if (!mutex.is_unique_locked()) // should have a unique write lock
							assert_inside_wthread++;

						std::this_thread::sleep_for(std::chrono::milliseconds(500));

						num_unique_locks--;
						running_threads--;
					});
			}

			blocker.wait();
			std::this_thread::sleep_for(std::chrono::milliseconds(50)); // wait for locks to be acquired

			Assert::AreEqual(num_readers, (int)mutex.num_shared_locks());
			Assert::AreEqual(num_readers, (int)running_threads);
			Assert::AreEqual(0, (int)mutex.num_unique_locks());
			Assert::AreEqual(0, (int)num_unique_locks);
			Assert::IsFalse(mutex.is_unique_locked());

			for (int i = 0; i < num_readers; i++)
				threads[i].join();

			Assert::AreEqual(num_writers, (int)mutex.num_unique_locks());
			Assert::IsTrue(mutex.is_unique_locked());
			Assert::AreEqual(1, (int)num_unique_locks);
			Assert::AreEqual(1, (int)running_threads);

			for (int i = num_readers; i < num_threads; i++)
				threads[i].join();

			Assert::AreEqual(0, assert_inside_wthread.load());
			Assert::AreEqual(0, (int)mutex.num_shared_locks());
			Assert::AreEqual(0, (int)mutex.num_unique_locks());
			Assert::IsFalse(mutex.is_unique_locked());
		}

	public:

		/* The following tests check that the shared_atomic_mutex works with std locks (std::shared_lock 
		and std::unique_lock) and also that upgradable_lock and shared_atomic_mutex work together.

		"Readers" and "writers" refer to shared and unique locks required for reading/writing to/from a
		cache. There can be multiple readers but only one writer.
		*/

		TEST_METHOD(test_readers_std_lock)
		{
			test_readers<std::shared_lock<shared_atomic_mutex>>();
		}

		TEST_METHOD(test_readers_upgradable_lock)
		{
			test_readers<upgradable_lock>();
		}

		TEST_METHOD(test_writers_std_lock)
		{
			test_writers<std::unique_lock<shared_atomic_mutex>>();
		}

		TEST_METHOD(test_writers_upgradable_lock)
		{
			test_writers<upgradable_lock_unique_wrapper>();
		}

		TEST_METHOD(test_writers_blocking_readers_std_lock)
		{
			test_writers_blocking_readers<std::shared_lock<shared_atomic_mutex>, std::unique_lock<shared_atomic_mutex>>();
		}

		TEST_METHOD(test_writers_blocking_readers_upgradable_lock)
		{
			test_writers_blocking_readers<upgradable_lock, upgradable_lock_unique_wrapper>();
		}

		TEST_METHOD(test_readers_blocking_writers_std_lock)
		{
			test_writers_blocking_readers<std::shared_lock<shared_atomic_mutex>, std::unique_lock<shared_atomic_mutex>>();
		}

		TEST_METHOD(test_readers_blocking_writers_upgradable_lock)
		{
			test_writers_blocking_readers<upgradable_lock, upgradable_lock_unique_wrapper>();
		}

		TEST_METHOD(test_random_readers_writers)
		{
			const int numthreadloops = 100;
			const int percent_chance_of_writer = 33;
			const int percent_chance_of_reader_then_writer = 33;

			auto seed = std::random_device()();
			std::default_random_engine engine(seed);
			std::uniform_int_distribution<int> uniform_dist(1, 100);

			shared_atomic_mutex mutex;

			std::vector<std::thread> threads;
			std::atomic<int> assert_inside_wthread = 0;
			std::atomic<int> assert_inside_rthread = 0;
			std::atomic<int> assert_inside_uthread = 0;
			for (int i = 0; i < num_threads; i++)
			{
				threads.emplace_back([&]()
					{
						for (int j = 0; j < numthreadloops; j++)
						{
							int r = uniform_dist(engine);
							if (r < percent_chance_of_writer)
							{
								// lock uniquely (writer lock)
								mutex.lock();
								if (!mutex.is_unique_locked())
									assert_inside_wthread++;
								std::this_thread::sleep_for(std::chrono::milliseconds(10));
								if (!mutex.is_unique_locked())
									++assert_inside_wthread;
								mutex.unlock();
							}
							else
							{
								// shared lock (reader lock)
								mutex.lock_shared();
								if (mutex.is_unique_locked())
									assert_inside_rthread++;
								std::this_thread::sleep_for(std::chrono::milliseconds(10));
								if (mutex.is_unique_locked())
									assert_inside_rthread++;

								if (r > 100 - percent_chance_of_reader_then_writer)
								{
									// upgrade the shared lock to a unique lock
									mutex.upgrade();
									if (!mutex.is_unique_locked())
										assert_inside_uthread++;
									std::this_thread::sleep_for(std::chrono::milliseconds(10));
									if (!mutex.is_unique_locked())
										assert_inside_uthread++;
									mutex.unlock();
								}
								else
								{
									mutex.unlock_shared();
								}
							}
						}
					});
			}

			for (auto& th : threads)
				th.join();

			Assert::AreEqual(0, assert_inside_wthread.load());
			Assert::AreEqual(0, assert_inside_rthread.load());
			Assert::AreEqual(0, assert_inside_uthread.load());
			Assert::AreEqual(0, (int)mutex.num_shared_locks());
			Assert::AreEqual(0, (int)mutex.num_unique_locks());
			Assert::IsFalse(mutex.is_unique_locked());
		}
	};
}
