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

#ifndef dap_typeinfo_h
#define dap_typeinfo_h

#include <functional>
#include <string>

namespace dap {

class any;
class Deserializer;
class Serializer;

struct TypeInfo {
  std::string name;
  size_t size;
  size_t alignment;
  std::function<void(void*)> construct;
  std::function<void(void* dst, const void* src)> copyConstruct;
  std::function<void(void*)> destruct;
  std::function<bool(Deserializer*, void*)> deserialize;
  std::function<bool(Serializer*, const void*)> serialize;
};

}  // namespace dap

#endif  // dap_typeinfo_h
