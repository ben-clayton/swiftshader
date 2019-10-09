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

#ifndef dap_json_serializer_h
#define dap_json_serializer_h

#include "typeof.h"

// Workaround for https://bugs.llvm.org/show_bug.cgi?id=37700
#define not/**/ !
#define and /**/&&
#define or /**/ ||
#include <nlohmann/json.hpp>

namespace dap {

struct JSONDeserializer : public Deserializer {
  JSONDeserializer(const nlohmann::json::value_type&);

  virtual bool deserialize(boolean* v) override;
  virtual bool deserialize(integer* v) override;
  virtual bool deserialize(number* v) override;
  virtual bool deserialize(string* v) override;
  virtual bool deserialize(any* v) override;
  virtual size_t count() override;
  virtual bool element(size_t i,
                       const std::function<bool(Deserializer*)>&) override;
  virtual bool field(const std::string& name,
                     const std::function<bool(Deserializer*)>& cb) override;

 private:
  const nlohmann::json::value_type& json;
};

struct JSONSerializer : public Serializer {
  JSONSerializer(nlohmann::json::value_type&);

  virtual bool serialize(boolean v) override;
  virtual bool serialize(integer v) override;
  virtual bool serialize(number v) override;
  virtual bool serialize(const string& v) override;
  virtual bool serialize(const any& v) override;
  virtual bool array(
      size_t count,
      const std::function<bool(size_t i, Serializer*)>&) override;
  virtual bool field(const std::string& name,
                     const std::function<bool(Serializer*)>& cb) override;
  virtual void remove() override;

 private:
  nlohmann::json::value_type& json;
  bool removed = false;
};

}  // namespace dap

#endif  // dap_json_serializer_h