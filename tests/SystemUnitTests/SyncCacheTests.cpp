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

#include "System/LRUCache.hpp"
#include "System/SyncCache.hpp"

#include "marl/defer.h"
#include "marl/scheduler.h"
#include "marl/waitgroup.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

template<typename CACHE>
class SyncCacheTest : public testing::Test
{
protected:
	using Cache = CACHE;
	using SyncCache = sw::SyncCache<Cache>;

	static constexpr const int numThreads = 8;

	void SetUp() override
	{
		marl::Scheduler::Config cfg;
		cfg.setWorkerThreadCount(numThreads);
		auto scheduler = new marl::Scheduler(cfg);
		scheduler->bind();
	}

	void TearDown() override
	{
		auto scheduler = marl::Scheduler::get();
		scheduler->unbind();
		delete scheduler;
	}

	template<typename Function>
	void runParallel(int count, Function &&f)
	{
		marl::WaitGroup wg;
		wg.add(count);
		for(int i = 0; i < count; i++)
		{
			marl::schedule([wg, &f] {
				defer(wg.done());
				f();
			});
		}
		wg.wait();
	}
};

////////////////////////////////////////////////////////////////////////////////
// Cache types
////////////////////////////////////////////////////////////////////////////////
typedef ::testing::Types<
    sw::LRUCache<std::string, std::string>,
    std::unordered_map<std::string, std::string> >
    CacheTypes;
TYPED_TEST_SUITE(SyncCacheTest, CacheTypes);

////////////////////////////////////////////////////////////////////////////////
// Empty
////////////////////////////////////////////////////////////////////////////////
TYPED_TEST(SyncCacheTest, SingleThread_Empty)
{
	typename TestFixture::SyncCache cache(8);
	ASSERT_EQ("", cache.get("one"));
}

TYPED_TEST(SyncCacheTest, SingleThread_AddGet)
{
	typename TestFixture::SyncCache cache(8);
	cache.add("one", "1");
	cache.add("two", "2");
	cache.add("three", "3");
	ASSERT_EQ("1", cache.get("one"));
	ASSERT_EQ("2", cache.get("two"));
	ASSERT_EQ("3", cache.get("three"));
	ASSERT_EQ("", cache.get("four"));
}

TYPED_TEST(SyncCacheTest, SingleThread_AddAll)
{
	typename TestFixture::SyncCache cacheA(8);
	cacheA.add("one", "1");
	cacheA.add("two", "2");
	cacheA.add("three", "3");
	typename TestFixture::SyncCache cacheB(8);
	cacheB.add("ten", "10");
	cacheB.addAll(cacheA);

	ASSERT_EQ("1", cacheB.get("one"));
	ASSERT_EQ("2", cacheB.get("two"));
	ASSERT_EQ("3", cacheB.get("three"));
	ASSERT_EQ("10", cacheB.get("ten"));
}

TYPED_TEST(SyncCacheTest, SingleThread_Clear)
{
	typename TestFixture::SyncCache cache(8);

	cache.add("one", "1");
	cache.add("two", "2");
	cache.add("three", "3");
	cache.clear();

	ASSERT_EQ("", cache.get("one"));
	ASSERT_EQ("", cache.get("two"));
	ASSERT_EQ("", cache.get("three"));
}

TYPED_TEST(SyncCacheTest, SingleThread_Foreach)
{
	typename TestFixture::SyncCache cache(8);
	cache.add("one", "1");
	cache.add("two", "2");
	cache.add("three", "3");
	int count = 0;
	cache.foreach([&](std::string key, std::string value) {
		if(key == "one")
		{
			ASSERT_EQ(value, "1");
		}
		else if(key == "two")
		{
			ASSERT_EQ(value, "2");
		}
		else if(key == "three")
		{
			ASSERT_EQ(value, "3");
		}
		else
		{
			FAIL() << "Unexpected key: " << key;
		}
		++count;
	});
	ASSERT_EQ(count, 3);
}

TYPED_TEST(SyncCacheTest, SingleThread_GetOrCreate)
{
	typename TestFixture::SyncCache cache(8);
	ASSERT_EQ("meow", cache.getOrCreate("cat", [] { return "meow"; }));
	ASSERT_EQ("meow", cache.getOrCreate("cat", [] {
		ADD_FAILURE() << "create unexpectedly called";
		return "xxx";
	}));
	ASSERT_EQ("woof", cache.getOrCreate("dog", [] { return "woof"; }));
	ASSERT_EQ("woof", cache.getOrCreate("dog", [] {
		ADD_FAILURE() << "create unexpectedly called";
		return "xxx";
	}));
}

TYPED_TEST(SyncCacheTest, MultiThread_AddGet)
{
	typename TestFixture::SyncCache cache(8);

	TestFixture::runParallel(1000, [&] {
		cache.add("one", "1");
		cache.add("two", "2");
		cache.add("three", "3");
		ASSERT_EQ("1", cache.get("one"));
		ASSERT_EQ("2", cache.get("two"));
		ASSERT_EQ("3", cache.get("three"));
		ASSERT_EQ("", cache.get("four"));
	});
}

TYPED_TEST(SyncCacheTest, MultiThread_AddAll)
{
	typename TestFixture::SyncCache cacheA(8);
	typename TestFixture::SyncCache cacheB(8);

	TestFixture::runParallel(1000, [&] {
		cacheA.add("one", "1");
		cacheA.add("two", "2");
		cacheA.add("three", "3");
		cacheB.add("ten", "10");
		cacheB.addAll(cacheA);
		ASSERT_EQ("1", cacheB.get("one"));
		ASSERT_EQ("2", cacheB.get("two"));
		ASSERT_EQ("3", cacheB.get("three"));
		ASSERT_EQ("10", cacheB.get("ten"));
	});
}

TYPED_TEST(SyncCacheTest, MultiThread_Clear)
{
	typename TestFixture::SyncCache cache(8);

	TestFixture::runParallel(1000, [&] {
		cache.add("one", "1");
		cache.add("two", "2");
		cache.add("three", "3");
		cache.clear();
	});

	ASSERT_EQ("", cache.get("one"));
	ASSERT_EQ("", cache.get("two"));
	ASSERT_EQ("", cache.get("three"));
}

TYPED_TEST(SyncCacheTest, MultiThread_Foreach)
{
	typename TestFixture::SyncCache cache(8);
	cache.add("one", "1");
	cache.add("two", "2");
	cache.add("three", "3");
	TestFixture::runParallel(1000, [&] {
		int count = 0;
		cache.foreach([&](std::string key, std::string value) {
			if(key == "one")
			{
				ASSERT_EQ(value, "1");
			}
			else if(key == "two")
			{
				ASSERT_EQ(value, "2");
			}
			else if(key == "three")
			{
				ASSERT_EQ(value, "3");
			}
			else
			{
				FAIL() << "Unexpected key: " << key;
			}
			++count;
		});
		ASSERT_EQ(count, 3);
	});
}

TYPED_TEST(SyncCacheTest, MultiThread_GetOrCreate)
{
	typename TestFixture::SyncCache cache(8);

	// Call getOrCreate() with create functions that block until unblock is
	// signalled.
	marl::WaitGroup wg(3);
	marl::Event unblock(marl::Event::Mode::Manual);
	marl::schedule([wg, unblock, &cache] {
		ASSERT_EQ("meow", cache.getOrCreate("cat", [=] {
			wg.done();
			unblock.wait();
			return "meow";
		}));
	});
	marl::schedule([wg, unblock, &cache] {
		ASSERT_EQ("woof", cache.getOrCreate("dog", [=] {
			wg.done();
			unblock.wait();
			return "woof";
		}));
	});
	marl::schedule([wg, unblock, &cache] {
		ASSERT_EQ("blub", cache.getOrCreate("fish", [=] {
			wg.done();
			unblock.wait();
			return "blub";
		}));
	});
	wg.wait();  // Wait for the 3 getOrCreate() calls to have entered create.

	// Spin up a bunch of concurrent tasks that also call getOrCreate().
	// All the cache entries have already already begun construction, so these
	// will block until the above create calls have finished.
	for(int i = 0; i < 1000; i++)
	{
		wg.add(3);
		marl::schedule([wg, &cache] {
			defer(wg.done());
			ASSERT_EQ("meow", cache.getOrCreate("cat", [=] {
				ADD_FAILURE() << "create unexpectedly called";
				return "xxx";
			}));
		});
		marl::schedule([wg, &cache] {
			defer(wg.done());
			ASSERT_EQ("woof", cache.getOrCreate("dog", [=] {
				ADD_FAILURE() << "create unexpectedly called";
				return "xxx";
			}));
		});
		marl::schedule([wg, &cache] {
			defer(wg.done());
			ASSERT_EQ("blub", cache.getOrCreate("fish", [=] {
				ADD_FAILURE() << "create unexpectedly called";
				return "xxx";
			}));
		});
	}

	unblock.signal();

	wg.wait();
}
