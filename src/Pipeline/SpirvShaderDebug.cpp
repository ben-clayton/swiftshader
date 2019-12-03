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

#include "SpirvShader.hpp"

#ifdef ENABLE_VK_DBG

#include "Vulkan/Debug/Context.hpp"
#include "Vulkan/Debug/File.hpp"
#include "Vulkan/Debug/Thread.hpp"
#include "Vulkan/Debug/Variable.hpp"

#include "spirv-tools/libspirv.h"

namespace spvtools {

// Function implemented in third_party/SPIRV-Tools/source/disassemble.cpp
// but with no public header.
// This is a C++ function, so the name is mangled, and signature changes will
// result in a linker error instead of runtime signature mismatches.
std::string spvInstructionBinaryToText(const spv_target_env env,
                                       const uint32_t* inst_binary,
                                       const size_t inst_word_count,
                                       const uint32_t* binary,
                                       const size_t word_count,
                                       const uint32_t options);

}  // namespace spvtools

namespace {

const char* laneNames[] = { "Lane 0", "Lane 1", "Lane 2", "Lane 3" };
static_assert(sizeof(laneNames) / sizeof(laneNames[0]) == sw::SIMD::Width,
	"laneNames must have SIMD::Width entries");

////////////////////////////////////////////////////////////////////////////////
// ::Ctx
//
// Holds the shader debugger state.
//
// It is interfaced via the sw::SpirvShader::Debug::Context and
// sw::SpirvShader::Debug::Group helper classes.
////////////////////////////////////////////////////////////////////////////////
class Ctx {
public:
	static Ctx* create(const sw::SpirvShader* shader, const char* stackBase);
	static void destroy(Ctx* context);

	void enter(vk::dbg::Context::Lock& lock, const char* name);
	void exit();
	void updateActiveLaneMask(int lane, bool enabled);
	void update(int line, vk::dbg::File::ID fileID);

	vk::dbg::VariableContainer* registers();
	vk::dbg::VariableContainer* locals();
	vk::dbg::VariableContainer* hovers();

	vk::dbg::VariableContainer* registersLane(int lane);
	vk::dbg::VariableContainer* localsLane(int lane);

	template<typename K>
	vk::dbg::VariableContainer* group(vk::dbg::VariableContainer* vc, K key);

	template<typename K, typename V>
	void put(vk::dbg::VariableContainer* vc, K key, V value);

private:
	template<typename T>
	static std::string tostring(const T& s);
	static std::string tostring(const char* s);
	static std::string tostring(sw::SpirvShader::Object::ID id);

	Ctx(const sw::SpirvShader* shader, const char* stackBase, vk::dbg::Context::Lock& lock);
	~Ctx();

	const sw::SpirvShader* shader;
	const std::shared_ptr<vk::dbg::Context> ctx;
	const std::shared_ptr<vk::dbg::Thread> thread;
	std::array<std::shared_ptr<vk::dbg::VariableContainer>, sw::SIMD::Width> registersByLane;
	std::array<std::shared_ptr<vk::dbg::VariableContainer>, sw::SIMD::Width> localsByLane;
};


Ctx* Ctx::create(const sw::SpirvShader* shader, const char* stackBase)
{
	auto lock = shader->dbg.ctx->lock();
	return new Ctx(shader, stackBase, lock);
}

void Ctx::destroy(Ctx* context) { delete context; }

void Ctx::enter(vk::dbg::Context::Lock& lock, const char* name) {
	thread->enter(lock, shader->dbg.spirvFile, name);
}

void Ctx::exit() { thread->exit(); }

void Ctx::updateActiveLaneMask(int lane, bool enabled)
{
	registersByLane[lane]->put("enabled", vk::dbg::make_constant(enabled));
}

void Ctx::update(int line, vk::dbg::File::ID fileID)
{
	auto file = ctx->lock().get(fileID);
	thread->update({line, file});
}

vk::dbg::VariableContainer* Ctx::registers() { return thread->registers().get(); }
vk::dbg::VariableContainer* Ctx::locals() { return thread->locals().get(); }
vk::dbg::VariableContainer* Ctx::hovers() { return thread->hovers().get(); }

vk::dbg::VariableContainer* Ctx::registersLane(int i) { return registersByLane[i].get(); }
vk::dbg::VariableContainer* Ctx::localsLane(int i) { return localsByLane[i].get(); }

template<typename K>
vk::dbg::VariableContainer* Ctx::group(vk::dbg::VariableContainer* vc, K key)
{
	auto out = ctx->lock().createVariableContainer();
	vc->put(tostring(key), out);
	return out.get();
}

template<typename K, typename V>
void Ctx::put(vk::dbg::VariableContainer* vc, K key, V value)
{
	vc->put(tostring(key), vk::dbg::make_constant(value));
}

template<typename T>
std::string Ctx::tostring(const T& s) { return std::to_string(s); }
std::string Ctx::tostring(const char* s) { return s; }
std::string Ctx::tostring(sw::SpirvShader::Object::ID id) { return "%" + std::to_string(id.value()); }

Ctx::Ctx(const sw::SpirvShader* shader, const char* stackBase, vk::dbg::Context::Lock& lock)
	: shader(shader)
	, ctx(shader->dbg.ctx)
	, thread(lock.currentThread())
{
	enter(lock, stackBase);
	for (int i = 0; i < sw::SIMD::Width; i++) {
		registersByLane[i] = lock.createVariableContainer();
		localsByLane[i] = lock.createVariableContainer();
		thread->registers()->put(laneNames[i], registersByLane[i]);
		thread->locals()->put(laneNames[i], localsByLane[i]);
	}
}

Ctx::~Ctx() { exit(); }

}  // anonymous namespace

namespace rr {

////////////////////////////////////////////////////////////////////////////////
// rr::CToReactor<T> specializations.
////////////////////////////////////////////////////////////////////////////////
template<>
struct CToReactor<sw::SpirvShader::Object::ID>
{
	using type = rr::Int;
	static rr::Int cast(sw::SpirvShader::Object::ID id) { return rr::Int(id.value()); }
};

template<typename T>
struct CToReactor<vk::dbg::ID<T>>
{
	using type = rr::Int;
	static rr::Int cast(vk::dbg::ID<T> id) { return rr::Int(id.value()); }
};

} // namespace rr

namespace sw {

////////////////////////////////////////////////////////////////////////////////
// sw::SpirvShader::Debug::Group
//
// This provides the SpirvShader interface to the group related APIs of the Ctx
// declared above.
////////////////////////////////////////////////////////////////////////////////
class SpirvShader::Debug::Group
{
public:
	using Ptr = rr::Pointer<rr::Byte>;

	Group(Ptr ctx, Ptr group);

	template<typename K, typename RK>
	Group group(RK key) const;

	template<typename K, typename V, typename RK, typename RV>
	void put(RK key, RV value) const;

	template<typename K, typename V, typename RK, typename RV>
	void put(RK key, RV x, RV y) const;

	template<typename K, typename V, typename RK, typename RV>
	void put(RK key, RV x, RV y, RV z) const;

	template<typename K, typename V, typename RK, typename RV>
	void put(RK key, RV x, RV y, RV z, RV w) const;

	template<typename K, typename V, typename VEC>
	void putVec3(K key, const VEC& v) const;

private:
	Ptr ctx;
	Ptr ptr;
};

SpirvShader::Debug::Group::Group(Ptr ctx, Ptr group) : ctx(ctx), ptr(group) {}

template<typename K, typename RK>
SpirvShader::Debug::Group SpirvShader::Debug::Group::group(RK key) const
{
	return Group(ctx, rr::Call(&Ctx::group<K>, ctx, ptr, key));
}

template<typename K, typename V, typename RK, typename RV>
void SpirvShader::Debug::Group::put(RK key, RV value) const
{
	rr::Call(&Ctx::put<K, V>, ctx, ptr, key, value);
}

template<typename K, typename V, typename RK, typename RV>
void SpirvShader::Debug::Group::put(RK key, RV x, RV y) const
{
	auto vec = group<K>(key);
	vec.template put<const char*, V>("x", x);
	vec.template put<const char*, V>("y", y);
}

template<typename K, typename V, typename RK, typename RV>
void SpirvShader::Debug::Group::put(RK key, RV x, RV y, RV z) const
{
	auto vec = group<K>(key);
	vec.template put<const char*, V>("x", x);
	vec.template put<const char*, V>("y", y);
	vec.template put<const char*, V>("z", z);
}

template<typename K, typename V, typename RK, typename RV>
void SpirvShader::Debug::Group::put(RK key, RV x, RV y, RV z, RV w) const
{
	auto vec = group<K>(key);
	vec.template put<const char*, V>("x", x);
	vec.template put<const char*, V>("y", y);
	vec.template put<const char*, V>("z", z);
	vec.template put<const char*, V>("w", w);
}

template<typename K, typename V, typename VEC>
void SpirvShader::Debug::Group::putVec3(K key, const VEC& v) const
{
	auto vec = group<K>(key);
	vec.template put<const char*, V>("x", Extract(v, 0));
	vec.template put<const char*, V>("y", Extract(v, 1));
	vec.template put<const char*, V>("z", Extract(v, 2));
}

////////////////////////////////////////////////////////////////////////////////
// sw::SpirvShader::Debug::Context
//
// This provides the SpirvShader interface to the Ctx class declared above.
////////////////////////////////////////////////////////////////////////////////
class SpirvShader::Debug::Context {
public:
	using Ptr = rr::Pointer<rr::Byte>;

	Context(Ptr ctx);

	static Ptr create(const sw::SpirvShader* shader, const char* name);
	static void destroy(Ptr ptr);

	void update(int line, vk::dbg::File::ID file) const;

	void updateActiveLaneMask(int lane, rr::Int enabled) const;

	Group registers() const;
	Group locals() const;
	Group hovers() const;

	Group registersLane(uint32_t lane) const;
	Group localsLane(uint32_t lane) const;

private:
	Ptr ctx;
};

SpirvShader::Debug::Context::Context(Ptr ctx) : ctx(ctx) {}

SpirvShader::Debug::Context::Ptr SpirvShader::Debug::Context::create(const sw::SpirvShader* shader, const char* name)
{
	return rr::Call(Ctx::create, rr::ConstantPointer(shader), name);
}

void SpirvShader::Debug::Context::destroy(Ptr ptr)
{
	return rr::Call(Ctx::destroy, ptr);
}

void SpirvShader::Debug::Context::update(int line, vk::dbg::File::ID file) const
{
	rr::Call(&Ctx::update, ctx, line, file);
}

void SpirvShader::Debug::Context::updateActiveLaneMask(int lane, rr::Int enabled) const
{
	rr::Call(&Ctx::updateActiveLaneMask, ctx, lane, enabled != 0);
}

SpirvShader::Debug::Group SpirvShader::Debug::Context::registers() const
{
	return Group(ctx, rr::Call(&Ctx::registers, ctx));
}

SpirvShader::Debug::Group SpirvShader::Debug::Context::locals() const
{
	return Group(ctx, rr::Call(&Ctx::locals, ctx));
}

SpirvShader::Debug::Group SpirvShader::Debug::Context::hovers() const
{
	return Group(ctx, rr::Call(&Ctx::hovers, ctx));
}

SpirvShader::Debug::Group SpirvShader::Debug::Context::registersLane(uint32_t lane) const
{
	return Group(ctx, rr::Call(&Ctx::registersLane, ctx, lane));
}

SpirvShader::Debug::Group SpirvShader::Debug::Context::localsLane(uint32_t lane) const
{
	return Group(ctx, rr::Call(&Ctx::localsLane, ctx, lane));
}

////////////////////////////////////////////////////////////////////////////////
// sw::SpirvShader
////////////////////////////////////////////////////////////////////////////////
using DC = SpirvShader::Debug::Context;

void SpirvShader::dbgCreateFile()
{
	if (!dbg.ctx) { return; }

	int currentLine = 1;
	std::string source;
	for (auto insn : *this) {
		auto instruction = spvtools::spvInstructionBinaryToText(
			SPV_ENV_VULKAN_1_1,
			insn.wordPointer(0),
			insn.wordCount(),
			insns.data(),
			insns.size(),
			SPV_BINARY_TO_TEXT_OPTION_NO_HEADER) + "\n";
		dbg.spirvLineMappings[insn.wordPointer(0)] = currentLine;
		currentLine += std::count(instruction.begin(), instruction.end(), '\n');
		source += instruction;
	}
	std::string name;
	switch (executionModel)
	{
		case spv::ExecutionModelVertex:    name = "VertexShader";   break;
		case spv::ExecutionModelFragment:  name = "FragmentShader"; break;
		case spv::ExecutionModelGLCompute: name = "ComputeShader";  break;
		default: name = "SPIR-V Shader"; break;
	}
	static std::atomic<int> id = { 0 };
	name += std::to_string(id++) + ".spvasm";
	dbg.spirvFile = dbg.ctx->lock().createVirtualFile(name.c_str(), source.c_str());
}

void SpirvShader::dbgBeginEmit(EmitState *state) const
{
	if (!dbg.ctx) { return; }

	auto routine = state->routine;

	auto type = "SPIR-V";
	switch (executionModel)
	{
		case spv::ExecutionModelVertex:    type = "VertexShader";   break;
		case spv::ExecutionModelFragment:  type = "FragmentShader"; break;
		case spv::ExecutionModelGLCompute: type = "ComputeShader";  break;
		default: type = "SPIR-V Shader"; break;
	}
	routine->dbgctx = DC::create(this, type);

	auto ctx = DC(routine->dbgctx);
	state->setActiveLaneMask(state->activeLaneMask(), dbg.ctx);

	auto locals = ctx.locals();
	locals.put<const char*, int>("subgroupSize", routine->invocationsPerSubgroup);

	switch (executionModel) {
	case spv::ExecutionModelGLCompute:
		locals.putVec3<const char*, int>("numWorkgroups", routine->numWorkgroups);
		locals.putVec3<const char*, int>("workgroupID", routine->workgroupID);
		locals.putVec3<const char*, int>("workgroupSize", routine->workgroupSize);
		locals.put<const char*, int>("numSubgroups", routine->subgroupsPerWorkgroup);
		locals.put<const char*, int>("subgroupIndex", routine->subgroupIndex);

		for (int i = 0; i < SIMD::Width; i++) {
			auto lane = ctx.localsLane(i);
			lane.put<const char*, int>("globalInvocationId",
				rr::Extract(routine->globalInvocationID[0], i),
				rr::Extract(routine->globalInvocationID[1], i),
				rr::Extract(routine->globalInvocationID[2], i));
			lane.put<const char*, int>("localInvocationId",
				rr::Extract(routine->localInvocationID[0], i),
				rr::Extract(routine->localInvocationID[1], i),
				rr::Extract(routine->localInvocationID[2], i));
			lane.put<const char*, int>("localInvocationIndex", rr::Extract(routine->localInvocationIndex, i));
		}
		break;

	case spv::ExecutionModelFragment:
		locals.put<const char*, int>("viewIndex", routine->viewID);
		for (int i = 0; i < SIMD::Width; i++) {
			auto lane = ctx.localsLane(i);
			lane.put<const char*, float>("fragCoord",
				rr::Extract(routine->fragCoord[0], i),
				rr::Extract(routine->fragCoord[1], i),
				rr::Extract(routine->fragCoord[2], i),
				rr::Extract(routine->fragCoord[3], i));
			lane.put<const char*, float>("pointCoord",
				rr::Extract(routine->pointCoord[0], i),
				rr::Extract(routine->pointCoord[1], i));
			lane.put<const char*, int>("windowSpacePosition",
				rr::Extract(routine->windowSpacePosition[0], i),
				rr::Extract(routine->windowSpacePosition[1], i));
			lane.put<const char*, int>("helperInvocation", rr::Extract(routine->helperInvocation, i));
		}
		break;

	case spv::ExecutionModelVertex:
		break;

	default:
		break;
	}
}

void SpirvShader::dbgEndEmit(EmitState *state) const
{
	if (!dbg.ctx) { return; }

	DC::destroy(state->routine->dbgctx);
}

void SpirvShader::dbgBeginEmitInstruction(InsnIterator insn, EmitState *state) const
{
	if (!dbg.ctx) { return; }

	auto line = dbg.spirvLineMappings.at(insn.wordPointer(0));
	DC(state->routine->dbgctx).update(line, dbg.spirvFile->id);
}

void SpirvShader::dbgEndEmitInstruction(InsnIterator insn, EmitState *state) const
{
	if (!dbg.ctx) { return; }

	auto resIt = dbg.results.find(insn.wordPointer(0));
	if (resIt != dbg.results.end())
	{
		auto id = resIt->second;
		dbgExposeIntermediate(id, state);
	}
}

void SpirvShader::dbgExposeIntermediate(Object::ID id, EmitState *state) const
{
	if (!dbg.ctx) { return; }

	dbgExposeVariable(id, id, state);
}

template<typename Key>
void SpirvShader::dbgExposeVariable(const Key& key, Object::ID id, EmitState *state) const
{
	if (!dbg.ctx) { return; }

	auto ctx = DC(state->routine->dbgctx);
	auto hover = ctx.hovers().group<Key>(key);
	for (int lane = 0; lane < SIMD::Width; lane++) {
		dbgExposeVariable(ctx.localsLane(lane), lane, key, id, state);
		dbgExposeVariable(hover, lane, laneNames[lane], id, state);
	}
}

template<typename Key>
void SpirvShader::dbgExposeVariable(const Debug::Group& group, int l, const Key& key, Object::ID id, EmitState *state) const
{
	if (!dbg.ctx) { return; }

	GenericValue val(this, state, id);
	auto ctx = DC(state->routine->dbgctx);
	switch (getType(val.type).opcode()) {
	case spv::OpTypeInt:
		group.put<Key, int>(key, Extract(val.Int(0), l));
		break;
	case spv::OpTypeFloat:
		group.put<Key, float>(key, Extract(val.Float(0), l));
		break;
	case spv::OpTypeVector: {
		auto count = getType(val.type).definition.word(3);
		switch (count) {
			case 1:
				group.put<Key, float>(key, Extract(val.Float(0), l));
				break;
			case 2:
				group.put<Key, float>(key, Extract(val.Float(0), l), Extract(val.Float(1), l));
				break;
			case 3:
				group.put<Key, float>(key, Extract(val.Float(0), l), Extract(val.Float(1), l), Extract(val.Float(2), l));
				break;
			case 4:
				group.put<Key, float>(key, Extract(val.Float(0), l), Extract(val.Float(1), l), Extract(val.Float(2), l), Extract(val.Float(3), l));
				break;
			default: {
				auto vec = group.group<Key>(key);
				for (uint32_t i = 0; i < count; i++) {
					vec.template put<int, float>(i, Extract(val.Float(i), l));
				}
				break;
			}
		}
		break;
	}
	case spv::OpTypePointer: {
		auto objectTy = getType(getObject(id).type);
		bool interleavedByLane = IsStorageInterleavedByLane(objectTy.storageClass);
		auto ptr = state->getPointer(id);
		auto ptrGroup = group.group<Key>(key);
		VisitMemoryObject(id, [&](const MemoryElement& el) {
			auto p = ptr + el.offset;
			if (interleavedByLane) { p = InterleaveByLane(p); }  // TODO: Interleave once, then add offset?
			auto simd = p.Load<SIMD::Float>(sw::OutOfBoundsBehavior::Nullify, state->activeLaneMask());
			ptrGroup.template put<int, float>(el.index, Extract(simd, l));
		});
		break;
	}
	default:
		break;
	}
}

////////////////////////////////////////////////////////////////////////////////
// sw::SpirvShader::EmitState
////////////////////////////////////////////////////////////////////////////////
void SpirvShader::EmitState::dbgUpdateActiveLaneMask(RValue<SIMD::Int> mask, const std::shared_ptr<vk::dbg::Context>& dbgctx)
{
	if (dbgctx)
	{
		for (int lane = 0; lane < SIMD::Width; lane++) {
			DC(routine->dbgctx).updateActiveLaneMask(lane, rr::Extract(mask, lane));
		}
	}
}

} // namespace sw

#else // ENABLE_VK_DBG

// Stub implementations of the dbgXXX functions.
namespace sw {

void SpirvShader::dbgCreateFile() {}
void SpirvShader::dbgBeginEmit(EmitState *state) const {}
void SpirvShader::dbgEndEmit(EmitState *state) const {}
void SpirvShader::dbgBeginEmitInstruction(InsnIterator insn, EmitState *state) const {}
void SpirvShader::dbgEndEmitInstruction(InsnIterator insn, EmitState *state) const {}
void SpirvShader::dbgExposeIntermediate(Object::ID id, EmitState *state) const {}

template<typename Key>
void SpirvShader::dbgExposeVariable(const Key& key, Object::ID id, EmitState *state) const {}

template<typename Key>
void SpirvShader::dbgExposeVariable(Debug::Group& group, int l, const Key& key, const GenericValue& val, Object::ID id, EmitState *state) const {}

void SpirvShader::EmitState::dbgUpdateActiveLaneMask(RValue<SIMD::Int> mask, const std::shared_ptr<vk::dbg::Context>& dbgctx) {}

} // namespace sw

#endif // ENABLE_VK_DBG

