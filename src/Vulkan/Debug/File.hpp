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

#ifndef VK_DEBUG_FILE_HPP_
#define VK_DEBUG_FILE_HPP_

#include "ID.hpp"

#include <memory>
#include <string>

namespace vk
{
namespace dbg
{

class File
{
public:
	using ID = dbg::ID<File>;

	static std::shared_ptr<File> createVirtual(ID id, const std::string& name, const std::string& source);
	static std::shared_ptr<File> createPhysical(ID id, const std::string& path);

	virtual inline ~File() = default;

	virtual void clearBreakpoints() = 0;
	virtual void addBreakpoint(int line) = 0;
	virtual bool hasBreakpoint(int line) const = 0;
	virtual bool isVirtual() const = 0;
	virtual std::string source() const = 0;

	inline std::string path() const;

	const ID id;
	const std::string dir;
	const std::string name;

protected:
	inline File(ID id, const std::string dir, const std::string name);
};

File::File(ID id, const std::string dir, const std::string name) :
    id(id),
    dir(dir),
    name(name) {}

std::string File::path() const
{
	auto d = dir;
	return (d.size() > 0) ? (d + "/" + name) : name;
}

}  // namespace dbg
}  // namespace vk

#endif  // VK_DEBUG_FILE_HPP_
