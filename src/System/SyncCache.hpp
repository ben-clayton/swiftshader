// Copyright 2020 The SwiftShader Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef sw_SyncCache_hpp
#define sw_SyncCache_hpp

#include "marl/event.h"
#include "marl/pool.h"

#include <map>
#include <unordered_map>

namespace sw {

////////////////////////////////////////////////////////////////////////////////
// Forward declarations
////////////////////////////////////////////////////////////////////////////////
template<typename KEY, typename DATA, typename HASH>
class LRUCache;

////////////////////////////////////////////////////////////////////////////////
// CacheTraits holds type traits for cache types.
// CacheTraits is used by SyncCache.
////////////////////////////////////////////////////////////////////////////////
template<typename T>
struct CacheTraits
{};

template<typename KEY, typename DATA, typename HASH>
struct CacheTraits<LRUCache<KEY, DATA, HASH>>
{
	using Key = KEY;
	using Data = DATA;
	using Cache = LRUCache<KEY, DATA, HASH>;
	using ConstIterator = typename Cache::iterator;

	template<typename V>
	using WithData = LRUCache<KEY, V, HASH>;

	static Data get(Cache &cache, const Key &key) { return cache.lookup(key); }
	static void add(Cache &cache, const Key &key, const Data &data) { cache.add(key, data); }
	static const Key &key(const ConstIterator &it) { return (*it).key(); }
	static const Data &data(const ConstIterator &it) { return (*it).data(); }
	static void clear(Cache &cache) { cache.clear(); }
};

template<typename KEY, typename DATA, typename MAP>
struct CacheMapBaseTraits
{
	using Key = KEY;
	using Data = DATA;
	using Cache = MAP;
	using ConstIterator = typename Cache::const_iterator;

	static Data get(Cache &cache, const Key &key)
	{
		auto it = cache.find(key);
		return it != cache.end() ? it->second : Data{};
	}
	static void add(Cache &cache, const Key &key, const Data &data) { cache.emplace(key, data); }
	static const Key &key(const ConstIterator &it) { return it->first; }
	static const Data &data(const ConstIterator &it) { return it->second; }
	static void clear(Cache &cache) { cache.clear(); }
};

template<typename KEY, typename DATA, typename COMPARE, typename ALLOCATOR>
struct CacheTraits<std::map<KEY, DATA, COMPARE, ALLOCATOR>> : public CacheMapBaseTraits<KEY, DATA, std::map<KEY, DATA, COMPARE, ALLOCATOR>>
{
	template<typename V>
	using AllocatorForData = typename std::allocator_traits<ALLOCATOR>::template rebind_alloc<std::pair<const KEY, V>>;

	template<typename V>
	using WithData = std::map<KEY, V, COMPARE, AllocatorForData<V>>;
};

template<typename KEY, typename DATA, typename COMPARE, typename KEY_EQUAL, typename ALLOCATOR>
struct CacheTraits<std::unordered_map<KEY, DATA, COMPARE, KEY_EQUAL, ALLOCATOR>> : public CacheMapBaseTraits<KEY, DATA, std::unordered_map<KEY, DATA, COMPARE, KEY_EQUAL, ALLOCATOR>>
{
	template<typename V>
	using AllocatorForData = typename std::allocator_traits<ALLOCATOR>::template rebind_alloc<std::pair<const KEY, V>>;

	template<typename V>
	using WithData = std::unordered_map<KEY, V, COMPARE, KEY_EQUAL, AllocatorForData<V>>;
};

////////////////////////////////////////////////////////////////////////////////
// SyncCache transforms a thread-unsafe cache or map of type CACHE into one
// that can used by multiple concurrent threads.
// SyncCache provides basic foreach(), get() and add() methods for querying and
// adding to the cache, as well as a getOrCreate() method for performing both in
// one atomic operation. getOrCreate() will ensure that the cache entry will
// only be created once even if multiple concurrent calls are made with the same
// key.
////////////////////////////////////////////////////////////////////////////////
template<typename CACHE>
class SyncCache
{
public:
	using Key = typename CacheTraits<CACHE>::Key;
	using Data = typename CacheTraits<CACHE>::Data;

	// The constructor forwards the arguments on to the underlying CACHE type.
	template<typename... ARGS>
	inline SyncCache(ARGS &&...);

	// foreach() calls callback with each cache entry's key and data.
	// Note that the internal mutex is held during traversal, so calling get()
	// or add() on the same thread will deadlock.
	// Callback must be a function of the signature:
	//     void(const Key &, const Data&)
	template<typename Callback>
	inline void foreach(Callback &&callback) const;

	// get() queries the cache for the entry with the given key.
	inline Data get(const Key &key);

	// add() adds to the cache a new entry with the given key and data,
	// replacing any existing entry with the same key.
	inline void add(const Key &key, const Data &data);

	// addAll() adds to the cache all entries from the other cache.
	inline void addAll(const SyncCache &other);

	// getOrCreate() looks for a cache entry with the given key, returning the
	// associated entry's data if found. If the cache does not hold an entry
	// with the given key, and this is the first thread to call getOrCreate()
	// for the given key, then create is called and the returned value is added
	// to the cache with the given key. If getOrCreate() is called by another
	// thread while the data for the given key is being built, the thread
	// will wait for the data to be built.
	// If a marl::scheduler is bound to the current thread, then the scheduler
	// may execute other tasks while waiting for a create function to complete.
	template<typename Create>
	inline Data getOrCreate(const Key &key, Create &&create);

	// clear() empties the cache of all entries.
	inline void clear();

private:
	struct Entry
	{
		Data data;
		marl::mutex mutex;
		marl::ConditionVariable cv;
		bool ready = false;
	};

	// Each SyncCache holds a pool of Entry.
	// Loans from this pool will be used as the data of the inner cache type.
	// If / when the inner cache type evicts its items, the Entry loan will be
	// automatically returned to the pool, and can be reused without another
	// heap allocation.
	// As a pool loan is essentially a shared pointer it can be tested for
	// nullability to determine a cache hit or miss.
	using EntryPool = marl::UnboundedPool<Entry>;
	using EntryLoan = typename EntryPool::Loan;
	using EntryCache = typename CacheTraits<CACHE>::template WithData<EntryLoan>;
	using Traits = CacheTraits<EntryCache>;

	EntryPool pool;

	EntryCache cache GUARDED_BY(mutex);
	mutable marl::mutex mutex;
};

template<typename CACHE>
template<typename... ARGS>
SyncCache<CACHE>::SyncCache(ARGS &&... args)
    : cache(std::forward<ARGS>(args)...)
{}

template<typename CACHE>
template<typename Callback>
void SyncCache<CACHE>::foreach(Callback &&callback) const
{
	marl::lock lock(mutex);
	for(auto it = std::begin(cache); it != std::end(cache); ++it)
	{
		callback(Traits::key(it), Traits::data(it)->data);
	}
}

template<typename CACHE>
typename SyncCache<CACHE>::Data SyncCache<CACHE>::get(const Key &key)
{
	EntryLoan entry;
	{
		marl::lock lock(mutex);
		entry = Traits::get(cache, key);
		if(!entry.get())
		{
			return Data{};  // No entry exists for this key.
		}
	}

	// Another thread is responsible for creating the data.
	// Wait until it is marked ready.
	{
		marl::lock lock(entry->mutex);
		entry->cv.wait(lock, [&] { return entry->ready; });
	}
	return entry->data;
}

template<typename CACHE>
void SyncCache<CACHE>::add(const Key &key, const Data &data)
{
	auto entry = pool.borrow();
	entry->data = data;
	entry->ready = true;

	marl::lock lock(mutex);
	Traits::add(cache, key, entry);
}

template<typename CACHE>
void SyncCache<CACHE>::addAll(const SyncCache &other)
{
	marl::lock lock(mutex);
	other.foreach([this](const Key &key, const Data &data) REQUIRES(mutex) {
		auto entry = pool.borrow();
		entry->data = data;
		entry->ready = true;
		Traits::add(cache, key, entry);
	});
}

template<typename CACHE>
template<typename Create>
typename SyncCache<CACHE>::Data SyncCache<CACHE>::getOrCreate(const Key &key, Create &&create)
{
	EntryLoan entry;
	bool doCreate = false;
	{
		// Under lock, lookup the entry for the given key
		marl::lock lock(mutex);
		entry = Traits::get(cache, key);
		// If the entry is nullptr, this is the thread to create the entry.
		if(!entry.get())
		{
			entry = pool.borrow();
			Traits::add(cache, key, entry);
			doCreate = true;
		}
	}

	if(doCreate)
	{
		// This thread is responsible for creating the data.
		// Call create() under no lock as this might take a while...
		entry->data = create();
		{
			marl::lock lock(entry->mutex);
			entry->ready = true;
		}
		entry->cv.notify_all();  // Notify other threads that this is ready!
	}
	else
	{
		// Another thread is responsible for creating the data.
		// Wait until it is marked ready.
		marl::lock lock(entry->mutex);
		entry->cv.wait(lock, [&] { return entry->ready; });
	}

	return entry->data;
}

template<typename CACHE>
void SyncCache<CACHE>::clear()
{
	marl::lock lock(mutex);
	Traits::clear(cache);
}

}  // namespace sw

#endif  // sw_SyncCache_hpp
