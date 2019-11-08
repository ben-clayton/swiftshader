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

#ifndef VK_DEBUG_SERVER_HPP_
#define VK_DEBUG_SERVER_HPP_

#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace vk
{
namespace dbg
{

class Thread;
struct VariableContainer;

class File
{
public:
	using Id = int;
	inline File(Id id) :
	    id(id) {}
	virtual inline ~File() = default;
	virtual std::string dir() const = 0;
	virtual std::string name() const = 0;
	virtual void clearBreakpoints() = 0;
	virtual void addBreakpoint(int line) = 0;
	virtual bool hasBreakpoint(int line) const = 0;
	virtual bool isVirtual() const = 0;

	std::string path() const
	{
		auto d = dir();
		if(d.size() > 0)
		{
			return d + "/" + name();
		}
		else
		{
			return name();
		}
	}

	const Id id;
};

struct Location
{
	inline Location() = default;
	inline Location(int line, const std::shared_ptr<File>& file);
	int line = 0;
	std::shared_ptr<File> file;
};

Location::Location(int line, const std::shared_ptr<File>& file) :
    line(line),
    file(file) {}

enum class Kind
{
	Bool,
	U8,
	S8,
	U16,
	S16,
	F32,
	U32,
	S32,
	F64,
	U64,
	S64,
	Ptr,
	VariableContainer,
};

struct Type
{
	inline Type() = default;
	inline Type(Kind kind) :
	    kind(kind) {}
	Kind kind;
	std::shared_ptr<Type> elem;
};

// clang-format off
template <typename T> struct TypeOf;
template <> struct TypeOf<bool>              { static std::shared_ptr<Type> get(); };
template <> struct TypeOf<uint8_t>           { static std::shared_ptr<Type> get(); };
template <> struct TypeOf<int8_t>            { static std::shared_ptr<Type> get(); };
template <> struct TypeOf<uint16_t>          { static std::shared_ptr<Type> get(); };
template <> struct TypeOf<int16_t>           { static std::shared_ptr<Type> get(); };
template <> struct TypeOf<float>             { static std::shared_ptr<Type> get(); };
template <> struct TypeOf<uint32_t>          { static std::shared_ptr<Type> get(); };
template <> struct TypeOf<int32_t>           { static std::shared_ptr<Type> get(); };
template <> struct TypeOf<double>            { static std::shared_ptr<Type> get(); };
template <> struct TypeOf<uint64_t>          { static std::shared_ptr<Type> get(); };
template <> struct TypeOf<int64_t>           { static std::shared_ptr<Type> get(); };
template <> struct TypeOf<VariableContainer> { static std::shared_ptr<Type> get(); };
// clang-format on

struct Value
{
	virtual ~Value() = default;
	virtual std::shared_ptr<Type> type() const = 0;
	virtual const void* get() const = 0;
	virtual bool set(void* ptr) { return false; }
};

template <typename T>
struct Constant : public Value
{
public:
	inline Constant(const T& value) :
	    value(value) {}
	inline std::shared_ptr<Type> type() const override
	{
		return TypeOf<T>::get();
	}
	inline const void* get() const override { return &value; }

private:
	const T value;
};

struct Variable
{
	std::string name;
	std::shared_ptr<Value> value;
};

struct VariableContainer : public Value
{
	using Id = int;

	inline VariableContainer(Id id) :
	    id(id) {}
	const Id id;

	template <typename F>
	inline void foreach(size_t startIndex, const F& cb) const;

	template <typename F>
	inline bool find(const std::string& name, const F& cb) const;

	inline void put(const Variable& var);
	inline void put(const std::string& name, const std::shared_ptr<Value>& value);

private:
	inline std::shared_ptr<Type> type() const override
	{
		return TypeOf<VariableContainer>::get();
	}

	inline const void* get() const override { return nullptr; }

	mutable std::mutex mutex;
	std::vector<Variable> variables;
	std::unordered_map<std::string, int> indices;
};

template <typename F>
void VariableContainer::foreach(size_t startIndex, const F& cb) const
{
	std::unique_lock<std::mutex> lock(mutex);
	for(size_t i = startIndex; i < variables.size(); i++)
	{
		cb(variables[i]);
	}
}

template <typename F>
bool VariableContainer::find(const std::string& name, const F& cb) const
{
	std::unique_lock<std::mutex> lock(mutex);
	for(auto const& var : variables)
	{
		if(var.name == name)
		{
			cb(var);
			return true;
		}
	}
	return false;
}

void VariableContainer::put(const Variable& var)
{
	std::unique_lock<std::mutex> lock(mutex);
	auto it = indices.find(var.name);
	if(it == indices.end())
	{
		indices.emplace(var.name, variables.size());
		variables.push_back(var);
	}
	else
	{
		variables[it->second].value = var.value;
	}
}

void VariableContainer::put(const std::string& name,
                            const std::shared_ptr<Value>& value)
{
	put({ name, value });
}

template <typename T>
inline std::shared_ptr<Constant<T>> make_constant(const T& value)
{
	return std::shared_ptr<Constant<T>>(new vk::dbg::Constant<T>(value));
}

struct Scope
{
	using Id = int;

	inline Scope(Id id,
	             const std::shared_ptr<File>& file,
	             const std::shared_ptr<VariableContainer>& variables) :
	    id(id),
	    file(file),
	    variables(variables) {}
	const Id id;
	const std::shared_ptr<File> file;
	const std::shared_ptr<VariableContainer> variables;
};

struct Frame
{
	using Id = int;

	inline Frame(Id id) :
	    id(id) {}
	const Id id;
	std::string function;
	Location location;
	std::shared_ptr<Scope> arguments;
	std::shared_ptr<Scope> locals;
	std::shared_ptr<Scope> registers;
};

class Server
{
public:
	static std::shared_ptr<Server> get();

	virtual ~Server() = default;
	virtual std::shared_ptr<Thread> currentThread() = 0;
	virtual std::shared_ptr<File> file(File::Id) = 0;
	virtual std::shared_ptr<File> createVirtualFile(const char* name,
	                                                const char* source) = 0;
	virtual std::shared_ptr<File> createPhysicalFile(
	    const char* name,
	    const char* dir,
	    const char* source = nullptr) = 0;
	virtual std::shared_ptr<VariableContainer> createVariableContainer() = 0;
	inline std::shared_ptr<File> createPhysicalFile(const char* path,
	                                                const char* source = nullptr);

private:
	friend class Thread;
	class Impl;
};

class Thread
{
public:
	using Id = int;

	enum class State
	{
		Running,
		Stepping,
		Paused
	};

	Thread(int id, Server::Impl* Server);
	void setName(const char*);
	std::string getName() const;
	void enter(const std::shared_ptr<File>& file, const char* function);
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

	const Id id;

private:
	void onStep();
	void onLineBreakpoint();
	void onFunctionBreakpoint();

	Server::Impl* const server;

	mutable std::mutex nameMutex;
	std::string name;  // guarded by nameMutex

	mutable std::mutex stateMutex;
	std::vector<std::shared_ptr<Frame>> frames;  // guarded by stateMutex
	std::condition_variable stateCV;             // guarded by stateMutex
	State state = State::Running;                // guarded by stateMutex
	std::shared_ptr<Frame> pauseAtFrame;         // guarded by stateMutex
};

std::shared_ptr<File> Server::createPhysicalFile(
    const char* path,
    const char* source /* = nullptr */)
{
	auto pathstr = std::string(path);
	auto pos = pathstr.rfind("/");
	if(pos != std::string::npos)
	{
		auto dir = pathstr.substr(0, pos);
		auto name = pathstr.substr(pos + 1);
		return createPhysicalFile(name.c_str(), dir.c_str(), source);
	}
	return createPhysicalFile("", path, source);
}

}  // namespace dbg
}  // namespace vk

#endif  // VK_DEBUG_SERVER_HPP_
