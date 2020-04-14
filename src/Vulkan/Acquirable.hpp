// Copyright 2018 The SwiftShader Authors. All Rights Reserved.
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

#ifndef VK_ACQUIRABLE_HPP_
#define VK_ACQUIRABLE_HPP_

#include "System/Debug.hpp"

#include "marl/event.h"
#include "marl/waitgroup.h"

namespace vk {

// Acquirable is a synchronization helper that prevents destruction of a Vulkan
// object while it is currently being used by a background task.
class Acquirable
{
public:
	inline ~Acquirable();

	// destroy() blocks until all calls to acquire() have a corresponding call
	// to release(). After destroy() has returned, attempting to call acquire()
	// will assert.
	inline void destroy();

	// acquire() is called before it is used by a background task.
	// While acquired, any calls to destroy() will block until the last call to
	// release() is made.
	// Attempting to call acquire() after destroy() is called will result in an
	// assert.
	inline void acquire();

	// release() is called once a background tasks has finished with the Vulkan
	// object.
	inline void release();

private:
	marl::WaitGroup refcount;
	marl::Event destroyed = { marl::Event::Mode::Manual };
};

Acquirable::~Acquirable()
{
	ASSERT_MSG(destroyed.test(), "Acquirable destructed without a call to Acquirable::destroy()");
}

void Acquirable::destroy()
{
	destroyed.signal();
	refcount.wait();
}

void Acquirable::acquire()
{
	ASSERT_MSG(!destroyed.test(), "Acquirable::acquire() called after Acquirable::destroy()");
	refcount.add();
}

void Acquirable::release()
{
	refcount.done();
}

}  // namespace vk

#endif  // VK_ACQUIRABLE_HPP_
