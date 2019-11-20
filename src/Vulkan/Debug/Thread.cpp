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

#include "Thread.hpp"

#include "Context.hpp"
#include "EventListener.hpp"
#include "File.hpp"

namespace vk
{
namespace dbg
{

Thread::Thread(ID id, Context* ctx) :
    id(id),
    broadcast(ctx->broadcast()) {}

void Thread::setName(const char* name)
{
	std::unique_lock<std::mutex> lock(mutex);
	this->name = name;
}

std::string Thread::getName() const
{
	std::unique_lock<std::mutex> lock(mutex);
	return name;
}

void Thread::update(const Location& location)
{
	std::unique_lock<std::mutex> lock(mutex);
	frames.back()->location = location;

	if(state == State::Running)
	{
		if(location.file->hasBreakpoint(location.line))
		{
			broadcast->onLineBreakpointHit(id);
			state = State::Paused;
		}
	}

	switch(state)
	{
	case State::Paused:
	{
		stateCV.wait(lock, [this] { return state != State::Paused; });
		break;
	}

	case State::Stepping:
	{
		if(!pauseAtFrame || pauseAtFrame == frames.back())
		{
			broadcast->onThreadStepped(id);
			state = State::Paused;
			stateCV.wait(lock, [this] { return state != State::Paused; });
			pauseAtFrame = 0;
		}
		break;
	}

	case State::Running:
		break;
	}
}

void Thread::enter(Context::Lock& ctxlck, const std::shared_ptr<File>& file, const char* function)
{
	auto frame = ctxlck.createFrame(file);
	auto isFunctionBreakpoint = ctxlck.isFunctionBreakpoint(function);

	std::unique_lock<std::mutex> lock(mutex);
	frame->function = function;
	frames.push_back(frame);
	if(isFunctionBreakpoint)
	{
		broadcast->onFunctionBreakpointHit(id);
		state = State::Paused;
	}
}

void Thread::exit()
{
	std::unique_lock<std::mutex> lock(mutex);
	frames.pop_back();
}

std::shared_ptr<VariableContainer> Thread::registers() const
{
	std::unique_lock<std::mutex> lock(mutex);
	return frames.back()->registers->variables;
}

std::shared_ptr<VariableContainer> Thread::locals() const
{
	std::unique_lock<std::mutex> lock(mutex);
	return frames.back()->locals->variables;
}

std::shared_ptr<VariableContainer> Thread::arguments() const
{
	std::unique_lock<std::mutex> lock(mutex);
	return frames.back()->arguments->variables;
}

std::vector<std::shared_ptr<Frame>> Thread::stack() const
{
	std::unique_lock<std::mutex> lock(mutex);
	return frames;
}

Thread::State Thread::getState() const
{
	std::unique_lock<std::mutex> lock(mutex);
	return state;
}

void Thread::resume()
{
	std::unique_lock<std::mutex> lock(mutex);
	state = State::Running;
	lock.unlock();
	stateCV.notify_all();
}

void Thread::pause()
{
	std::unique_lock<std::mutex> lock(mutex);
	state = State::Paused;
}

void Thread::stepIn()
{
	std::unique_lock<std::mutex> lock(mutex);
	state = State::Stepping;
	pauseAtFrame.reset();
	stateCV.notify_all();
}

void Thread::stepOver()
{
	std::unique_lock<std::mutex> lock(mutex);
	state = State::Stepping;
	pauseAtFrame = frames.back();
	stateCV.notify_all();
}

void Thread::stepOut()
{
	std::unique_lock<std::mutex> lock(mutex);
	state = State::Stepping;
	pauseAtFrame = (frames.size() > 1) ? frames[frames.size() - 1] : nullptr;
	stateCV.notify_all();
}

}  // namespace dbg
}  // namespace vk