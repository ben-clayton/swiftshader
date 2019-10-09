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

#include "json_serializer.h"

namespace {

struct NullDeserializer : public dap::Deserializer {
  static NullDeserializer instance;

  inline virtual bool deserialize(dap::boolean* v) override { return false; }
  inline virtual bool deserialize(dap::integer* v) override { return false; }
  inline virtual bool deserialize(dap::number* v) override { return false; }
  inline virtual bool deserialize(dap::string* v) override { return false; }
  inline virtual bool deserialize(dap::any* v) override { return false; }
  inline virtual size_t count() override { return 0; }
  inline virtual bool element(
      size_t i,
      const std::function<bool(Deserializer*)>&) override {
    return false;
  }
  inline virtual bool field(
      const std::string& name,
      const std::function<bool(Deserializer*)>& cb) override {
    return false;
  }
};

NullDeserializer NullDeserializer::instance;

}  // anonymous namespace

namespace dap {

JSONDeserializer::JSONDeserializer(const nlohmann::json::value_type& json)
    : json(json) {}

bool JSONDeserializer::deserialize(boolean* v) {
  if (!json.is_boolean()) {
    return false;
  }
  *v = json;
  return true;
}

bool JSONDeserializer::deserialize(integer* v) {
  if (!json.is_number_integer()) {
    return false;
  }
  *v = json;
  return true;
}

bool JSONDeserializer::deserialize(number* v) {
  if (!json.is_number()) {
    return false;
  }
  *v = json.get<double>();
  return true;
}

bool JSONDeserializer::deserialize(string* v) {
  if (!json.is_string()) {
    return false;
  }
  *v = json;
  return true;
}

bool JSONDeserializer::deserialize(any* v) {
  if (json.is_boolean()) {
    *v = boolean(json.get<bool>());
  } else if (json.is_number_float()) {
    *v = number(json.get<double>());
  } else if (json.is_number_integer()) {
    *v = integer(json.get<int>());
  } else if (json.is_string()) {
    *v = json.get<string>();
  } else {
    return false;
  }
  return true;
}

size_t JSONDeserializer::count() {
  return json.size();
}

bool JSONDeserializer::element(size_t i,
                               const std::function<bool(Deserializer*)>& cb) {
  JSONDeserializer d(json[i]);
  return cb(&d);
}

bool JSONDeserializer::field(const std::string& name,
                             const std::function<bool(Deserializer*)>& cb) {
  auto it = json.find(name);
  if (it == json.end()) {
    return cb(&NullDeserializer::instance);
  }
  JSONDeserializer d(*it);
  return cb(&d);
}

JSONSerializer::JSONSerializer(nlohmann::json::value_type& json) : json(json) {}

bool JSONSerializer::serialize(boolean v) {
  json = (bool)v;
  return true;
}

bool JSONSerializer::serialize(integer v) {
  json = (int)v;
  return true;
}

bool JSONSerializer::serialize(number v) {
  json = (double)v;
  return true;
}

bool JSONSerializer::serialize(const string& v) {
  json = v;
  return true;
}

bool JSONSerializer::serialize(const any& v) {
  if (v.is<boolean>()) {
    json = (bool)v.get<boolean>();
  } else if (v.is<integer>()) {
    json = (int)v.get<integer>();
  } else if (v.is<number>()) {
    json = (double)v.get<number>();
  } else if (v.is<string>()) {
    json = v.get<string>();
  } else {
    return false;
  }

  return true;
}

bool JSONSerializer::array(
    size_t count,
    const std::function<bool(size_t i, Serializer*)>& cb) {
  json = std::vector<int>();
  for (size_t i = 0; i < count; i++) {
    JSONSerializer s(json[i]);
    if (!cb(i, &s)) {
      return false;
    }
  }
  return true;
}

bool JSONSerializer::field(const std::string& name,
                           const std::function<bool(Serializer*)>& cb) {
  JSONSerializer s(json[name]);
  auto res = cb(&s);
  if (s.removed) {
    json.erase(name);
  }
  return res;
}

void JSONSerializer::remove() {
  removed = true;
}

}  // namespace dap
