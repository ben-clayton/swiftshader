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

#ifndef VK_DEBUG_VARIABLE_HPP_
#define VK_DEBUG_VARIABLE_HPP_

#include "ID.hpp"
#include "Type.hpp"
#include "Value.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace vk
{
namespace dbg
{

struct Variable
{
	std::string name;
	std::shared_ptr<Value> value;
};

class VariableContainer : public Value
{
public:
	using ID = ID<VariableContainer>;

	inline VariableContainer(ID id);

	template <typename F>
	inline void foreach(size_t startIndex, const F& cb) const;

	template <typename F>
	inline bool find(const std::string& name, const F& cb) const;

	inline void put(const Variable& var);
	inline void put(const std::string& name, const std::shared_ptr<Value>& value);

	const ID id;

private:
	inline std::shared_ptr<Type> type() const override;
	inline const void* get() const override;

	mutable std::mutex mutex;
	std::vector<Variable> variables;
	std::unordered_map<std::string, int> indices;
};

VariableContainer::VariableContainer(ID id) :
    id(id) {}

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

std::shared_ptr<Type> VariableContainer::type() const
{
	return TypeOf<VariableContainer>::get();
}

const void* VariableContainer::get() const
{
	return nullptr;
}

}  // namespace dbg
}  // namespace vk

#endif  // VK_DEBUG_VARIABLE_HPP_