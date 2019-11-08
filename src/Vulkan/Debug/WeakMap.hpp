// Copyright 2019 The SwiftShader Authors. All Rights Reserved.
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

#ifndef VK_DEBUG_WEAKMAP_HPP_
#define VK_DEBUG_WEAKMAP_HPP_

#include <map>
#include <memory>

namespace vk
{
namespace dbg
{

template <typename K, typename V>
class WeakMap
{
	using Map = std::map<K, std::weak_ptr<V>>;
	using MapIterator = typename Map::const_iterator;

public:
	class iterator
	{
	public:
		inline iterator(const MapIterator& it, const MapIterator& end);
		inline void operator++();
		inline bool operator==(const iterator&) const;
		inline bool operator!=(const iterator&) const;
		inline std::pair<K, std::shared_ptr<V>> operator*() const;

	private:
		void skipNull();

		MapIterator it;
		const MapIterator end;
		std::shared_ptr<V> sptr;
	};

	inline iterator begin() const;
	inline iterator end() const;

	inline std::shared_ptr<V> get(const K& key) const;
	inline void add(const K& key, const std::shared_ptr<V>& val);
	inline void remove(const K& key);

private:
	inline void reap();

	Map map;
	size_t reapAtSize = 32;
};

template <typename K, typename V>
WeakMap<K, V>::iterator::iterator(const MapIterator& it, const MapIterator& end) :
    it(it),
    end(end)
{
	skipNull();
}

template <typename K, typename V>
void WeakMap<K, V>::iterator::operator++()
{
	it++;
	skipNull();
}

template <typename K, typename V>
void WeakMap<K, V>::iterator::skipNull()
{
	for(; it != end; ++it)
	{
		sptr = it->second.lock();
		if(sptr)
		{
			return;
		}
	}
}

template <typename K, typename V>
bool WeakMap<K, V>::iterator::operator==(const iterator& rhs) const
{
	return it == rhs.it;
}

template <typename K, typename V>
bool WeakMap<K, V>::iterator::operator!=(const iterator& rhs) const
{
	return it != rhs.it;
}

template <typename K, typename V>
std::pair<K, std::shared_ptr<V>> WeakMap<K, V>::iterator::operator*() const
{
	return { it->first, sptr };
}

template <typename K, typename V>
typename WeakMap<K, V>::iterator WeakMap<K, V>::begin() const
{
	return iterator(map.begin(), map.end());
}

template <typename K, typename V>
typename WeakMap<K, V>::iterator WeakMap<K, V>::end() const
{
	return iterator(map.end(), map.end());
}

template <typename K, typename V>
std::shared_ptr<V> WeakMap<K, V>::get(const K& key) const
{
	auto it = map.find(key);
	return (it != map.end()) ? it->second.lock() : nullptr;
}

template <typename K, typename V>
void WeakMap<K, V>::add(const K& key, const std::shared_ptr<V>& val)
{
	if(map.size() > reapAtSize)
	{
		reap();
		reapAtSize = map.size() * 2 + 32;
	}
	map.emplace(key, val);
}

template <typename K, typename V>
void WeakMap<K, V>::remove(const K& key)
{
	map.erase(key);
}

template <typename K, typename V>
void WeakMap<K, V>::reap()
{
	for(auto it = map.begin(); it != map.end();)
	{
		if(it->second.expired())
		{
			map.erase(it++);
		}
		else
		{
			++it;
		}
	}
}

}  // namespace dbg
}  // namespace vk

#endif  // VK_DEBUG_WEAKMAP_HPP_
