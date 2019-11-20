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

#ifndef VK_DEBUG_VALUE_HPP_
#define VK_DEBUG_VALUE_HPP_

#include <memory>

namespace vk
{
namespace dbg
{

class Type;

class Value
{
public:
	virtual ~Value() = default;
	virtual std::shared_ptr<Type> type() const = 0;
	virtual std::string string() const;
	virtual const void* get() const = 0;
	virtual bool set(void* ptr) { return false; }
};

template <typename T>
class Constant : public Value
{
public:
	inline Constant(const T& value);
	inline std::shared_ptr<Type> type() const override;
	inline const void* get() const override;

private:
	const T value;
};

template <typename T>
Constant<T>::Constant(const T& value) :
    value(value)
{
}

template <typename T>
std::shared_ptr<Type> Constant<T>::type() const
{
	return TypeOf<T>::get();
}

template <typename T>
const void* Constant<T>::get() const
{
	return &value;
}

template <typename T>
inline std::shared_ptr<Constant<T>> make_constant(const T& value)
{
	return std::shared_ptr<Constant<T>>(new vk::dbg::Constant<T>(value));
}

}  // namespace dbg
}  // namespace vk

#endif  // VK_DEBUG_VALUE_HPP_