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

#ifndef VK_DEBUG_THREAD_HPP_
#define VK_DEBUG_THREAD_HPP_

#include "Context.hpp"
#include "ID.hpp"
#include "Location.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace vk
{
namespace dbg
{

class File;
class VariableContainer;
class EventListener;

class Scope
{
public:
	using ID = ID<Scope>;

	inline Scope(ID id,
	             const std::shared_ptr<File>& file,
	             const std::shared_ptr<VariableContainer>& variables);
	const ID id;
	const std::shared_ptr<File> file;
	const std::shared_ptr<VariableContainer> variables;
};

Scope::Scope(ID id,
             const std::shared_ptr<File>& file,
             const std::shared_ptr<VariableContainer>& variables) :
    id(id),
    file(file),
    variables(variables) {}

class Frame
{
public:
	using ID = ID<Frame>;

	inline Frame(ID id);

	const ID id;
	std::string function;
	Location location;
	std::shared_ptr<Scope> arguments;
	std::shared_ptr<Scope> locals;
	std::shared_ptr<Scope> registers;
};

Frame::Frame(ID id) :
    id(id) {}

class Thread
{
public:
	using ID = ID<Thread>;

	enum class State
	{
		Running,
		Stepping,
		Paused
	};

	Thread(ID id, Context* ctx);

	void setName(const char*);
	std::string getName() const;
	void enter(Context::Lock& lock, const std::shared_ptr<File>& file, const char* function);
	void exit();
	std::shared_ptr<VariableContainer> registers() const;
	std::shared_ptr<VariableContainer> locals() const;
	std::shared_ptr<VariableContainer> arguments() const;
	std::vector<std::shared_ptr<Frame>> stack() const;
	State getState() const;
	void resume();
	void pause();
	void stepIn();
	void stepOver();
	void stepOut();

	void update(const Location& location);

	ID const id;

private:
	EventListener* const broadcast;

	mutable std::mutex mutex;
	std::string name;
	std::vector<std::shared_ptr<Frame>> frames;
	std::condition_variable stateCV;
	State state = State::Running;
	std::shared_ptr<Frame> pauseAtFrame;
};

}  // namespace dbg
}  // namespace vk

#endif  // VK_DEBUG_THREAD_HPP_
