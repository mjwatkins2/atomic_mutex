#include "CppUnitTest.h"

#include <shared_atomic_mutex/upgradable_lock.h>
#include <shared_atomic_mutex/shared_atomic_mutex.h>

#include <chrono>
#include <map>
#include <mutex>
#include <random>
#include <shared_mutex>
#include <sstream>
#include <thread>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace atomic_mutex;

namespace atomic_mutex_tests
{
	/* Demonstrates speed advantage of the upgradable shared_atomic_mutex for a multi-reader
	single-writer data cache. */
	TEST_CLASS(atomic_mutex_cache_example)
	{
	private:

		// Used as a simple reference upgradable lock for comparison
		class upgradable_lock_std
		{
		public:
			upgradable_lock_std(std::shared_mutex& m)
				: m_has_shared(false)
				, m_has_unique(false)
				, m_shared(&m)
			{
				shared_lock();
			}

			~upgradable_lock_std()
			{
				if (m_has_shared)
					m_shared->unlock_shared();
				else if (m_has_unique)
					m_shared->unlock();
			}

			void shared_lock()
			{
				m_shared->lock_shared();
				m_has_shared = true;
			}

			void upgrade()
			{
				m_upgrading.lock();
				m_shared->unlock_shared();
				m_shared->lock();
				m_upgrading.unlock();
				m_has_shared = false;
				m_has_unique = true;
			}

		private:
			std::shared_mutex* m_shared;
			bool m_has_shared;
			bool m_has_unique;
			std::mutex m_upgrading;
		};


		class cache
		{
		public:
			virtual double get_value(int key) = 0;

		protected:
			double expensive_operation(int val)
			{
				// do some fake "expensive" work
				return std::sqrt(val);
			}

			std::map<int, double> m_cache;
		};

		// Example cache protected by std::lock_guard and std::mutex for both reading and writing
		class cache_with_lock_guard : public cache
		{
		public:
			double get_value(int key) override
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				auto it = m_cache.find(key);
				if (it != m_cache.end())
					return it->second;

				return m_cache.emplace(std::make_pair(key, expensive_operation(key))).first->second;
			}

		private:
			std::mutex m_mutex;
		};


		// Example cache protected by shared_atomic_mutex and its upgradable multi-reader single-writer lock
		class cache_with_upgradable_lock : public cache
		{
		public:
			double get_value(int key) override
			{
				upgradable_lock lock(m_mutex);
				auto it = m_cache.find(key);
				if (it != m_cache.end())
					return it->second;

				lock.upgrade();
				// if expensive_operation is truly expensive, here it would be worth doing a second cache lookup
				return m_cache.emplace(std::make_pair(key, expensive_operation(key))).first->second;
			}

		private:
			shared_atomic_mutex m_mutex;
		};


		// Example cache protected by upgradable lock based on std mutexes
		class cache_with_upgradable_lock_std : public cache
		{
		public:
			double get_value(int key) override
			{
				upgradable_lock_std lock(m_mutex);
				auto it = m_cache.find(key);
				if (it != m_cache.end())
					return it->second;

				lock.upgrade();
				return m_cache.emplace(std::make_pair(key, expensive_operation(key))).first->second;
			}

		private:
			std::shared_mutex m_mutex;
		};

		double test_cache(cache& test_cache)
		{
			const int num_threads = 12;
			const int num_cache_reads_per_thread = 100000;
			const int max_key = 50;

			std::random_device rd;
			std::default_random_engine engine(rd());
			std::uniform_int_distribution<int> uniform_dist(1, max_key);

			// precompute all the random numbers
			std::vector<std::vector<int>> keys;
			keys.resize(num_threads);
			for (auto& keyvec : keys)
			{
				keyvec.reserve(num_cache_reads_per_thread);
				for (int j = 0; j < num_cache_reads_per_thread; j++)
					keyvec.push_back(uniform_dist(engine));
			}
			// Assert::AreEqual is slow, track asserts outside the threads 
			std::vector<int> asserts;
			asserts.resize(num_threads, 0);

			std::vector<std::thread> threads;
			auto t_start = std::chrono::high_resolution_clock::now();
			for (int i = 0; i < num_threads; i++)
			{
				threads.emplace_back([&, i]()
					{
						auto& keyvec = keys[i];
						auto key = keyvec.begin();
						while (key != keyvec.end())
						{
							double val = test_cache.get_value(*key);
							if (abs(val * val - *key) > 1e-10)
								asserts[i]++; // each thread has its own value so no race conditions here
							key++;
						}
					});
			}

			for (auto& th : threads)
				th.join();

			auto t_end = std::chrono::high_resolution_clock::now();

			for (auto a : asserts)
				Assert::AreEqual(0, a);

			return std::chrono::duration<double, std::milli>(t_end - t_start).count();
		}


		TEST_METHOD(test_cache_examples)
		{
			double time1 = test_cache(cache_with_lock_guard());
			double time2 = test_cache(cache_with_upgradable_lock_std());
			double time3 = test_cache(cache_with_upgradable_lock());

			std::ostringstream oss;
			oss << "\nOne lock guard: " << time1 << "\n"
				<< "Two mutexes:    " << time2 << "\n"
				<< "Atomic:         " << time3 << "\n"
				<< "Ratio1:          " << (time3 / time1) << "\n"
				<< "Ratio2:          " << (time3 / time2) << "\n";

			Logger::WriteMessage(oss.str().c_str());
		}
	};
}
