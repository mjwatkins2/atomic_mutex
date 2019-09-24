#pragma once

#include "shared_atomic_mutex.h"

namespace atomic_mutex
{
	/* Provides similar capability as std::shared_lock but designed to work with atomic_mutex::shared_atomic_mutex 
	in order to support upgrading from a shared to unique lock in a single atomic operation.

	Example usage:
		shared_atomic_mutex m;
		{
			upgradable_lock lock(m);
			*read stuff from a cache on this line*
			if (*need to write to the cache*)
			{
				lock.upgrade();
				*write stuff to cache*
			}
		}
	*/
	class upgradable_lock
	{
	public:
		// Constructs and locks the mutex in shared or unique mode.
		upgradable_lock(shared_atomic_mutex& m, bool unique = false)
			: m_mutex(&m)
			, m_owns_shared(false)
			, m_owns_unique(false)
		{
			if (unique)
				lock_unique();
			else
				lock_shared();
		}

		~upgradable_lock()
		{
			if (m_owns_shared)
				m_mutex->unlock_shared();
			else if (m_owns_unique)
				m_mutex->unlock();
		}

		void lock_unique()
		{
			assert(!m_owns_unique);
			if (m_owns_unique)
				return;
			else if (m_owns_shared)
				upgrade();
			else
			{
				m_mutex->lock();
				m_owns_unique = true;
			}
		}

		void lock_shared()
		{
			assert(!m_owns_unique);
			if (m_owns_unique)
				return;
			m_mutex->lock_shared();
			m_owns_shared = true;
		}

		void upgrade()
		{
			assert(m_owns_shared);
			if (m_owns_shared)
			{
				m_mutex->upgrade();
				m_owns_shared = false;
				m_owns_unique = true;
			}
		}

		void unlock()
		{
			if (m_owns_shared)
			{
				m_mutex->unlock_shared();
				m_owns_shared = false;
			}
			else if (m_owns_unique)
			{
				m_mutex->unlock();
				m_owns_unique = false;
			}
			assert(m_owns_shared == m_owns_unique);
		}

	private:

		shared_atomic_mutex* m_mutex;
		bool m_owns_shared;
		bool m_owns_unique;
	};
}
