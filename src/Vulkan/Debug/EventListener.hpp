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

#ifndef VK_DEBUG_EVENT_LISTENER_HPP_
#define VK_DEBUG_EVENT_LISTENER_HPP_

#include "ID.hpp"

namespace vk
{
namespace dbg
{

class Thread;

class EventListener
{
public:
	virtual ~EventListener() = default;
	virtual void onThreadStarted(ID<Thread>) {}
	virtual void onThreadStepped(ID<Thread>) {}
	virtual void onLineBreakpointHit(ID<Thread>) {}
	virtual void onFunctionBreakpointHit(ID<Thread>) {}
};

}  // namespace dbg
}  // namespace vk

#endif  // VK_DEBUG_EVENT_LISTENER_HPP_
