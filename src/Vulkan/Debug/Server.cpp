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

#include "Server.hpp"

#include "WeakMap.hpp"

#include "dap/network.h"
#include "dap/protocol.h"
#include "dap/session.h"
#include "marl/waitgroup.h"

#include <thread>
#include <unordered_set>

namespace
{

constexpr int port = 19020;

struct VirtualFile : public vk::dbg::File
{
	inline VirtualFile(const int id, const char* name, const char* source) :
	    File(id),
	    name_(name),
	    source(source ? source : "") {}

	std::string dir() const override;
	std::string name() const override;
	void clearBreakpoints() override;
	void addBreakpoint(int line) override;
	bool hasBreakpoint(int line) const override;
	bool isVirtual() const override;

	const std::string name_;
	const std::string source;

	mutable std::mutex breakpointMutex;
	std::unordered_set<int> breakpoints;  // guarded by breakpointMutex
};

std::string VirtualFile::dir() const
{
	return "";
}
std::string VirtualFile::name() const
{
	return name_;
}
void VirtualFile::clearBreakpoints()
{
	std::unique_lock<std::mutex> lock(breakpointMutex);
	breakpoints.clear();
}
void VirtualFile::addBreakpoint(int line)
{
	std::unique_lock<std::mutex> lock(breakpointMutex);
	breakpoints.emplace(line);
}
bool VirtualFile::hasBreakpoint(int line) const
{
	std::unique_lock<std::mutex> lock(breakpointMutex);
	return breakpoints.count(line) > 0;
}
bool VirtualFile::isVirtual() const
{
	return true;
}

struct PhysicalFile : public VirtualFile
{
	inline PhysicalFile(const int id,
	                    const char* name,
	                    const char* dir,
	                    const char* source) :
	    VirtualFile(id, name, source),
	    dir_(dir) {}

	std::string dir() const override;
	bool isVirtual() const override;

	const std::string dir_;
};

std::string PhysicalFile::dir() const
{
	return dir_;
}
bool PhysicalFile::isVirtual() const
{
	return false;
}

struct Files
{
	using SourceBreakpoints = dap::array<dap::SourceBreakpoint>;

	template <typename F>
	void foreach(const F&);

	std::shared_ptr<vk::dbg::File> add(
	    const std::shared_ptr<vk::dbg::File>& file);
	std::shared_ptr<vk::dbg::File> get(vk::dbg::File::Id id) const;

	void setPendingBreakpoints(const char* name, const SourceBreakpoints& bps);

private:
	mutable std::mutex mutex;
	std::unordered_map<vk::dbg::File::Id, std::shared_ptr<vk::dbg::File>> byId;
	std::unordered_map<std::string, SourceBreakpoints> pendingBreakpoints;
};

template <typename F>
void Files::foreach(const F& f)
{
	std::unique_lock<std::mutex> lock(mutex);
	for(auto it : byId)
	{
		f(it.first, it.second);
	}
}

std::shared_ptr<vk::dbg::File> Files::add(
    const std::shared_ptr<vk::dbg::File>& file)
{
	std::unique_lock<std::mutex> lock(mutex);
	byId.emplace(file->id, file);

	auto it = pendingBreakpoints.find(file->name());
	if(it != pendingBreakpoints.end())
	{
		for(auto bp : it->second)
		{
			file->addBreakpoint(bp.line);
		}
	}
	return file;
}

std::shared_ptr<vk::dbg::File> Files::get(vk::dbg::File::Id id) const
{
	std::unique_lock<std::mutex> lock(mutex);
	auto it = byId.find(id);
	return it != byId.end() ? it->second : nullptr;
}

void Files::setPendingBreakpoints(const char* name,
                                  const SourceBreakpoints& bps)
{
	std::unique_lock<std::mutex> lock(mutex);
	pendingBreakpoints.emplace(name, bps);
}

}  // anonymous namespace

namespace vk
{
namespace dbg
{

// clang-format off
std::shared_ptr<Type> TypeOf<bool>::get()              { static auto ty = std::make_shared<Type>(Kind::Bool); return ty; }
std::shared_ptr<Type> TypeOf<uint8_t>::get()           { static auto ty = std::make_shared<Type>(Kind::U8);   return ty; }
std::shared_ptr<Type> TypeOf<int8_t>::get()            { static auto ty = std::make_shared<Type>(Kind::S8);   return ty; }
std::shared_ptr<Type> TypeOf<uint16_t>::get()          { static auto ty = std::make_shared<Type>(Kind::U16);  return ty; }
std::shared_ptr<Type> TypeOf<int16_t>::get()           { static auto ty = std::make_shared<Type>(Kind::S16);  return ty; }
std::shared_ptr<Type> TypeOf<float>::get()             { static auto ty = std::make_shared<Type>(Kind::F32);  return ty; }
std::shared_ptr<Type> TypeOf<uint32_t>::get()          { static auto ty = std::make_shared<Type>(Kind::U32);  return ty; }
std::shared_ptr<Type> TypeOf<int32_t>::get()           { static auto ty = std::make_shared<Type>(Kind::S32);  return ty; }
std::shared_ptr<Type> TypeOf<double>::get()            { static auto ty = std::make_shared<Type>(Kind::F64);  return ty; }
std::shared_ptr<Type> TypeOf<uint64_t>::get()          { static auto ty = std::make_shared<Type>(Kind::U64);  return ty; }
std::shared_ptr<Type> TypeOf<int64_t>::get()           { static auto ty = std::make_shared<Type>(Kind::S64);  return ty; }
std::shared_ptr<Type> TypeOf<VariableContainer>::get() { static auto ty = std::make_shared<Type>(Kind::VariableContainer); return ty; }
// clang-format on

class Server::Impl : public Server
{
public:
	Impl();
	~Impl();

	std::shared_ptr<Thread> currentThread() override;
	std::shared_ptr<File> file(File::Id id) override;
	std::shared_ptr<File> createVirtualFile(const char* name,
	                                        const char* source) override;
	std::shared_ptr<File> createPhysicalFile(const char* name,
	                                         const char* dir,
	                                         const char* source) override;
	std::shared_ptr<VariableContainer> createVariableContainer() override;

	std::shared_ptr<Scope> createScope(const std::shared_ptr<File>& file);
	std::shared_ptr<Frame> createFrame(const std::shared_ptr<File>& file);

	bool isFunctionBreakpoint(const char*);

private:
	friend class Thread;

	dap::Scope scope(const char* type, Scope*);
	dap::Source source(File*);
	std::shared_ptr<File> file(const dap::Source& source);
	std::string type(const Type*);
	std::string value(const Value*);

	mutable std::recursive_mutex mutex;
	std::unique_ptr<dap::net::Server> server;
	std::unique_ptr<dap::Session> session;
	Files files;
	std::unordered_map<std::thread::id, std::shared_ptr<Thread>> threadsByStdId;
	WeakMap<Thread::Id, Thread> threads;
	WeakMap<VariableContainer::Id, VariableContainer> variableContainers;
	WeakMap<Frame::Id, Frame> frames;
	WeakMap<Scope::Id, Scope> scopes;
	std::unordered_set<std::string> functionBreakpoints;
	std::atomic<Thread::Id> nextThreadId = { 1 };
	std::atomic<File::Id> nextFileId = { 1 };
	std::atomic<VariableContainer::Id> nextVariableContainerId = { 1 };
	std::atomic<Frame::Id> nextFrameId = { 1 };
	std::atomic<Frame::Id> nextScopeId = { 1 };
	bool clientIsVisualStudio = false;
};

Server::Impl::Impl() :
    server(dap::net::Server::create()),
    session(dap::Session::create())
{
	session->registerHandler([](const dap::DisconnectRequest& req) {
		printf("DisconnectRequest receieved\n");
		return dap::DisconnectResponse();
	});

	session->registerHandler([&](const dap::InitializeRequest& req) {
		printf("InitializeRequest receieved\n");
		dap::InitializeResponse response;
		response.supportsFunctionBreakpoints = true;
		response.supportsConfigurationDoneRequest = true;
		clientIsVisualStudio = (req.clientID.value("") == "visualstudio");
		return response;
	});

	session->registerSentHandler(
	    [&](const dap::ResponseOrError<dap::InitializeResponse>& response) {
		    printf("InitializeResponse sent\n");
		    session->send(dap::InitializedEvent());
	    });

	session->registerHandler([](const dap::SetExceptionBreakpointsRequest& req) {
		printf("SetExceptionBreakpointsRequest receieved\n");
		dap::SetExceptionBreakpointsResponse response;
		return response;
	});

	session->registerHandler(
	    [this](const dap::SetFunctionBreakpointsRequest& req) {
		    printf("SetFunctionBreakpointsRequest receieved\n");
		    std::unique_lock<std::recursive_mutex> lock(mutex);
		    dap::SetFunctionBreakpointsResponse response;
		    for(auto const& bp : req.breakpoints)
		    {
			    functionBreakpoints.emplace(bp.name);
			    response.breakpoints.push_back({});
		    }
		    return response;
	    });

	session->registerHandler(
	    [this](const dap::SetBreakpointsRequest& req)
	        -> dap::ResponseOrError<dap::SetBreakpointsResponse> {
		    printf("SetBreakpointsRequest receieved\n");
		    bool verified = false;

		    size_t numBreakpoints = 0;
		    if(req.breakpoints.has_value())
		    {
			    auto const& breakpoints = req.breakpoints.value();
			    numBreakpoints = breakpoints.size();
			    if(auto file = this->file(req.source))
			    {
				    file->clearBreakpoints();
				    for(auto const& bp : breakpoints)
				    {
					    file->addBreakpoint(bp.line);
				    }
				    verified = true;
			    }
			    else if(req.source.name.has_value())
			    {
				    files.setPendingBreakpoints(req.source.name.value().c_str(),
				                                req.breakpoints.value());
			    }
		    }

		    dap::SetBreakpointsResponse response;
		    for(size_t i = 0; i < numBreakpoints; i++)
		    {
			    dap::Breakpoint bp;
			    bp.verified = verified;
			    bp.source = req.source;
			    response.breakpoints.push_back(bp);
		    }
		    return response;
	    });

	session->registerHandler([this](const dap::ThreadsRequest& req) {
		printf("ThreadsRequest receieved\n");
		std::unique_lock<std::recursive_mutex> lock(mutex);
		dap::ThreadsResponse response;
		for(auto it : threads)
		{
			auto thread = it.second;

			dap::Thread out;
			out.id = thread->id;
			out.name = thread->getName();
			response.threads.push_back(out);
		};
		return response;
	});

	session->registerHandler(
	    [this](const dap::StackTraceRequest& req)
	        -> dap::ResponseOrError<dap::StackTraceResponse> {
		    printf("StackTraceRequest receieved\n");

		    std::unique_lock<std::recursive_mutex> lock(mutex);
		    auto thread = threads.get(req.threadId);
		    if(!thread)
		    {
			    return dap::Error("Thread %d not found", req.threadId);
		    }

		    auto stack = thread->stack();

		    dap::StackTraceResponse response;
		    response.totalFrames = stack.size();
		    response.stackFrames.reserve(stack.size());
		    for(auto const& frame : stack)
		    {
			    auto const& loc = frame->location;
			    dap::StackFrame sf;
			    sf.column = 0;
			    sf.id = frame->id;
			    sf.name = frame->function;
			    sf.line = loc.line;
			    if(loc.file)
			    {
				    sf.source = source(loc.file.get());
			    }
			    response.stackFrames.emplace_back(std::move(sf));
		    }
		    return response;
	    });

	session->registerHandler([this](const dap::ScopesRequest& req)
	                             -> dap::ResponseOrError<dap::ScopesResponse> {
		printf("ScopesRequest receieved\n");

		std::unique_lock<std::recursive_mutex> lock(mutex);
		auto frame = frames.get(req.frameId);
		if(!frame)
		{
			return dap::Error("Frame %d not found", req.frameId);
		}

		dap::ScopesResponse response;
		response.scopes = {
			scope("locals", frame->locals.get()),
			scope("arguments", frame->arguments.get()),
			scope("registers", frame->registers.get()),
		};
		return response;
	});

	session->registerHandler([this](const dap::VariablesRequest& req)
	                             -> dap::ResponseOrError<dap::VariablesResponse> {
		printf("VariablesRequest receieved\n");

		std::unique_lock<std::recursive_mutex> lock(mutex);
		auto vars = variableContainers.get(req.variablesReference);
		if(!vars)
		{
			return dap::Error("VariablesReference %d not found",
			                  req.variablesReference);
		}

		dap::VariablesResponse response;
		vars->foreach(req.start.value(0), [&](const Variable& v) {
			if(!req.count.has_value() ||
			   req.count.value() < int(response.variables.size()))
			{
				dap::Variable out;
				out.evaluateName = v.name;
				out.name = v.name;
				out.type = type(v.value->type().get());
				out.value = value(v.value.get());
				if(v.value->type()->kind == Kind::VariableContainer)
				{
					auto const vc = static_cast<const VariableContainer*>(v.value.get());
					out.variablesReference = vc->id;
				}
				response.variables.push_back(out);
			}
		});
		return response;
	});

	session->registerHandler([this](const dap::SourceRequest& req)
	                             -> dap::ResponseOrError<dap::SourceResponse> {
		printf("SourceRequest receieved\n");
		dap::SourceResponse response;
		uint64_t id = req.sourceReference;
		std::unique_lock<std::recursive_mutex> lock(mutex);
		auto file = this->file(id);
		if(!file)
		{
			return dap::Error("Source %d not found", id);
		}
		auto vfile = static_cast<VirtualFile*>(file.get());
		response.content = vfile->source;
		return response;
	});

	session->registerHandler([this](const dap::PauseRequest& req)
	                             -> dap::ResponseOrError<dap::PauseResponse> {
		printf("PauseRequest receieved\n");

		dap::StoppedEvent event;
		event.reason = "pause";

		std::unique_lock<std::recursive_mutex> lock(mutex);
		if(auto thread = threads.get(req.threadId))
		{
			thread->pause();
			event.threadId = req.threadId;
		}
		else
		{
			for(auto it : threads)
			{
				it.second->pause();
			}
			event.allThreadsStopped = true;

			// Workaround for
			// https://github.com/microsoft/VSDebugAdapterHost/issues/11
			for(auto it : threads)
			{
				event.threadId = it.first;
			}
		}

		session->send(event);

		dap::PauseResponse response;
		return response;
	});

	session->registerHandler([this](const dap::ContinueRequest& req)
	                             -> dap::ResponseOrError<dap::ContinueResponse> {
		printf("ContinueRequest receieved\n");

		dap::ContinueResponse response;

		if(auto thread = threads.get(req.threadId))
		{
			thread->resume();
			response.allThreadsContinued = false;
		}
		else
		{
			for(auto it : threads)
			{
				thread->resume();
			}
			response.allThreadsContinued = true;
		}

		return response;
	});

	session->registerHandler([this](const dap::NextRequest& req)
	                             -> dap::ResponseOrError<dap::NextResponse> {
		printf("NextRequest receieved\n");

		auto thread = threads.get(req.threadId);
		if(!thread)
		{
			return dap::Error("Unknown thread %d", int(req.threadId));
		}

		thread->stepOver();
		return dap::NextResponse();
	});

	session->registerHandler([this](const dap::StepInRequest& req)
	                             -> dap::ResponseOrError<dap::StepInResponse> {
		printf("StepInRequest receieved\n");

		auto thread = threads.get(req.threadId);
		if(!thread)
		{
			return dap::Error("Unknown thread %d", int(req.threadId));
		}

		thread->stepIn();
		return dap::StepInResponse();
	});

	session->registerHandler([this](const dap::StepOutRequest& req)
	                             -> dap::ResponseOrError<dap::StepOutResponse> {
		printf("StepOutRequest receieved\n");

		auto thread = threads.get(req.threadId);
		if(!thread)
		{
			return dap::Error("Unknown thread %d", int(req.threadId));
		}

		thread->stepOut();
		return dap::StepOutResponse();
	});

	session->registerHandler([this](const dap::EvaluateRequest& req)
	                             -> dap::ResponseOrError<dap::EvaluateResponse> {
		printf("EvaluateRequest receieved\n");

		std::unique_lock<std::recursive_mutex> lock(mutex);
		if(req.frameId.has_value())
		{
			auto frame = frames.get(req.frameId.value());
			if(!frame)
			{
				return dap::Error("Unknown frame %d", int(req.frameId.value()));
			}

			dap::EvaluateResponse response;
			auto findHandler = [&](const Variable& var) {
				response.result = value(var.value.get());
				response.type = type(var.value->type().get());
			};
			if(frame->locals->variables->find(req.expression, findHandler) ||
			   frame->arguments->variables->find(req.expression, findHandler) ||
			   frame->registers->variables->find(req.expression, findHandler))
			{
				return response;
			}
		}

		return dap::Error("Could not evaluate expression");
	});

	session->registerHandler([](const dap::LaunchRequest& req) {
		printf("LaunchRequest receieved\n");
		return dap::LaunchResponse();
	});

	marl::WaitGroup configurationDone(1);
	session->registerHandler([=](const dap::ConfigurationDoneRequest& req) {
		printf("ConfigurationDoneRequest receieved\n");
		configurationDone.done();
		return dap::ConfigurationDoneResponse();
	});

	printf("Waiting for debugger connection...\n");
	server->start(port, [&](const std::shared_ptr<dap::ReaderWriter>& rw) {
		session->bind(rw);
	});
	configurationDone.wait();
}

Server::Impl::~Impl()
{
	server->stop();
}

std::shared_ptr<Thread> Server::Impl::currentThread()
{
	std::unique_lock<std::recursive_mutex> lock(mutex);
	auto threadIt = threadsByStdId.find(std::this_thread::get_id());
	if(threadIt != threadsByStdId.end())
	{
		return threadIt->second;
	}
	auto id = ++nextThreadId;
	char name[256];
	snprintf(name, sizeof(name), "Thread<0x%x>", int(id));

	auto thread = std::make_shared<Thread>(id, this);
	threads.add(id, thread);
	thread->setName(name);
	threadsByStdId.emplace(std::this_thread::get_id(), thread);

	dap::ThreadEvent event;
	event.reason = "started";
	event.threadId = id;
	session->send(event);
	return thread;
}

std::shared_ptr<File> Server::Impl::file(File::Id id)
{
	return files.get(id);
}

dap::Scope Server::Impl::scope(const char* type, Scope* s)
{
	dap::Scope out;
	// out.line = s->startLine;
	// out.endLine = s->endLine;
	out.source = source(s->file.get());
	out.name = type;
	out.presentationHint = type;
	out.variablesReference = s->variables->id;
	return out;
}

dap::Source Server::Impl::source(File* file)
{
	auto f = reinterpret_cast<VirtualFile*>(file);
	dap::Source out;
	out.name = f->name();
	if(file->isVirtual())
	{
		out.sourceReference = f->id;
	}
	else
	{
		out.path = file->path();
	}
	return out;
}

std::shared_ptr<File> Server::Impl::file(const dap::Source& source)
{
	if(source.sourceReference.has_value())
	{
		auto id = source.sourceReference.value();
		if(auto file = files.get(id))
		{
			return file;
		}
	}

	if(source.path.has_value())
	{
		auto path = source.path.value();
		std::shared_ptr<File> out;
		files.foreach([&](File::Id, const std::shared_ptr<File>& file) {
			if(file->path() == path)
			{
				out = file;
			}
		});
		return out;
	}

	if(source.name.has_value())
	{
		auto name = source.name.value();
		std::shared_ptr<File> out;
		files.foreach([&](File::Id, const std::shared_ptr<File>& file) {
			if(file->name() == name)
			{
				out = file;
			}
		});
		return out;
	}

	return nullptr;
}

std::string Server::Impl::type(const Type* ty)
{
	switch(ty->kind)
	{
	case Kind::Bool:
		return "bool";
	case Kind::U8:
		return "uint8_t";
	case Kind::S8:
		return "int8_t";
	case Kind::U16:
		return "uint16_t";
	case Kind::S16:
		return "int16_t";
	case Kind::F32:
		return "float";
	case Kind::U32:
		return "uint32_t";
	case Kind::S32:
		return "int32_t";
	case Kind::F64:
		return "double";
	case Kind::U64:
		return "uint64_t";
	case Kind::S64:
		return "int64_t";
	case Kind::Ptr:
		return type(ty->elem.get()) + "*";
	case Kind::VariableContainer:
		return "struct";
	}
	return "";
}

std::string Server::Impl::value(const Value* val)
{
	switch(val->type()->kind)
	{
	case Kind::Bool:
		return *reinterpret_cast<const bool*>(val->get()) ? "true" : "false";
	case Kind::U8:
		return std::to_string(*reinterpret_cast<const uint8_t*>(val->get()));
	case Kind::S8:
		return std::to_string(*reinterpret_cast<const int8_t*>(val->get()));
	case Kind::U16:
		return std::to_string(*reinterpret_cast<const uint16_t*>(val->get()));
	case Kind::S16:
		return std::to_string(*reinterpret_cast<const int16_t*>(val->get()));
	case Kind::F32:
		return std::to_string(*reinterpret_cast<const float*>(val->get()));
	case Kind::U32:
		return std::to_string(*reinterpret_cast<const uint32_t*>(val->get()));
	case Kind::S32:
		return std::to_string(*reinterpret_cast<const int32_t*>(val->get()));
	case Kind::F64:
		return std::to_string(*reinterpret_cast<const double*>(val->get()));
	case Kind::U64:
		return std::to_string(*reinterpret_cast<const uint64_t*>(val->get()));
	case Kind::S64:
		return std::to_string(*reinterpret_cast<const int64_t*>(val->get()));
	case Kind::Ptr:
		return std::to_string(reinterpret_cast<uintptr_t>(val->get()));
	case Kind::VariableContainer:
		auto const* vc = static_cast<const VariableContainer*>(val);
		std::string out = "";
		vc->foreach(0, [&](const Variable& var) {
			if(out.size() > 0)
			{
				out += ", ";
			}
			out += var.name;
			out += ": ";
			out += value(var.value.get());
		});
		return "[" + out + "]";
	}
	return "";
}

std::shared_ptr<File> Server::Impl::createVirtualFile(const char* name,
                                                      const char* source)
{
	auto id = nextFileId++;
	std::string sanitizedName = name;
	if(clientIsVisualStudio)
	{  // WORKAROUND:
		// https://github.com/microsoft/VSDebugAdapterHost/issues/15
		for(size_t i = 0; i < sanitizedName.size(); i++)
		{
			if(sanitizedName[i] == '.')
			{
				sanitizedName[i] = '_';
			}
		}
	}
	return files.add(
	    std::make_shared<VirtualFile>(id, sanitizedName.c_str(), source));
}

std::shared_ptr<File> Server::Impl::createPhysicalFile(const char* name,
                                                       const char* dir,
                                                       const char* source)
{
	auto id = nextFileId++;
	return files.add(std::make_shared<PhysicalFile>(id, name, dir, source));
}

std::shared_ptr<VariableContainer> Server::Impl::createVariableContainer()
{
	std::unique_lock<std::recursive_mutex> lock(mutex);
	auto container = std::make_shared<VariableContainer>(nextVariableContainerId++);
	variableContainers.add(container->id, container);
	return container;
}

std::shared_ptr<Scope> Server::Impl::createScope(
    const std::shared_ptr<File>& file)
{
	std::unique_lock<std::recursive_mutex> lock(mutex);
	auto scope = std::make_shared<Scope>(nextScopeId++, file, createVariableContainer());
	scopes.add(scope->id, scope);
	return scope;
}

std::shared_ptr<Frame> Server::Impl::createFrame(
    const std::shared_ptr<File>& file)
{
	std::unique_lock<std::recursive_mutex> lock(mutex);
	auto frame = std::make_shared<Frame>(nextFrameId++);
	frames.add(frame->id, frame);
	frame->arguments = createScope(file);
	frame->locals = createScope(file);
	frame->registers = createScope(file);
	return frame;
}

bool Server::Impl::isFunctionBreakpoint(const char* name)
{
	std::unique_lock<std::recursive_mutex> lock(mutex);
	return functionBreakpoints.count(name) > 0;
}

std::shared_ptr<Server> Server::get()
{
	static std::mutex mutex;
	static std::weak_ptr<Server> serverWeak;
	std::unique_lock<std::mutex> lock(mutex);
	auto server = serverWeak.lock();
	if(!server)
	{
		server = std::shared_ptr<Server>(new Server::Impl());
		serverWeak = server;
	}
	return server;
}

Thread::Thread(int id, Server::Impl* server) :
    id(id),
    server(server) {}

void Thread::setName(const char* name)
{
	std::unique_lock<std::mutex> lock(nameMutex);
	this->name = name;
}

std::string Thread::getName() const
{
	std::unique_lock<std::mutex> lock(nameMutex);
	return name;
}

void Thread::update(const Location& location)
{
	assert(location.file != nullptr);
	std::unique_lock<std::mutex> lock(stateMutex);
	frames.back()->location = location;

	if(state == State::Running)
	{
		if(location.file->hasBreakpoint(location.line))
		{
			onLineBreakpoint();
			state = State::Paused;
		}
	}

	switch(state)
	{
	case State::Paused:
		stateCV.wait(lock, [this] { return state != State::Paused; });
		break;
	case State::Stepping:
	{
		if(!pauseAtFrame || pauseAtFrame == frames.back())
		{
			onStep();
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

void Thread::onStep()
{
	dap::StoppedEvent event;
	event.threadId = id;
	event.reason = "step";
	server->session->send(event);
}

void Thread::onLineBreakpoint()
{
	dap::StoppedEvent event;
	event.threadId = id;
	event.reason = "breakpoint";
	server->session->send(event);
}

void Thread::onFunctionBreakpoint()
{
	dap::StoppedEvent event;
	event.threadId = id;
	event.reason = "function breakpoint";
	server->session->send(event);
}

void Thread::enter(const std::shared_ptr<File>& file, const char* function)
{
	auto frame = server->createFrame(file);
	auto isFunctionBreakpoint = server->isFunctionBreakpoint(function);

	std::unique_lock<std::mutex> lock(stateMutex);
	frame->function = function;
	frames.push_back(frame);
	if(isFunctionBreakpoint)
	{
		onFunctionBreakpoint();
		state = State::Paused;
	}
}

void Thread::exit()
{
	std::unique_lock<std::mutex> lock(stateMutex);
	frames.pop_back();
}

std::shared_ptr<VariableContainer> Thread::registers() const
{
	std::unique_lock<std::mutex> lock(stateMutex);
	return frames.back()->registers->variables;
}

std::shared_ptr<VariableContainer> Thread::locals() const
{
	std::unique_lock<std::mutex> lock(stateMutex);
	return frames.back()->locals->variables;
}

std::shared_ptr<VariableContainer> Thread::arguments() const
{
	std::unique_lock<std::mutex> lock(stateMutex);
	return frames.back()->arguments->variables;
}

std::vector<std::shared_ptr<Frame>> Thread::stack() const
{
	std::unique_lock<std::mutex> lock(stateMutex);
	return frames;
}

Thread::State Thread::getState() const
{
	std::unique_lock<std::mutex> lock(stateMutex);
	return state;
}

void Thread::resume()
{
	std::unique_lock<std::mutex> lock(stateMutex);
	state = State::Running;
	lock.unlock();
	stateCV.notify_all();
}

void Thread::pause()
{
	std::unique_lock<std::mutex> lock(stateMutex);
	state = State::Paused;
}

void Thread::stepIn()
{
	std::unique_lock<std::mutex> lock(stateMutex);
	state = State::Stepping;
	pauseAtFrame.reset();
	stateCV.notify_all();
}

void Thread::stepOver()
{
	std::unique_lock<std::mutex> lock(stateMutex);
	state = State::Stepping;
	pauseAtFrame = frames.back();
	stateCV.notify_all();
}

void Thread::stepOut()
{
	std::unique_lock<std::mutex> lock(stateMutex);
	state = State::Stepping;
	pauseAtFrame = (frames.size() > 1) ? frames[frames.size() - 1] : nullptr;
	stateCV.notify_all();
}

}  // namespace dbg
}  // namespace vk