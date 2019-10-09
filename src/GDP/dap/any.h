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

#ifndef dap_any_h
#define dap_any_h

#include "typeinfo.h"

#include <assert.h>

namespace dap {

template <typename T>
struct TypeOf;

class any {
 public:
  inline any() = default;
  inline any(const any& other) noexcept;
  inline any(any&& other) noexcept;
  inline ~any();

  template <typename T>
  inline any(const T& val);

  inline void reset();

  inline any& operator=(const any& rhs);
  inline any& operator=(any&& rhs) noexcept;

  template <typename T>
  inline any& operator=(const T& val);

  template <typename T>
  inline T& get() const;

  template <typename T>
  inline bool is() const;

 private:
  static inline void* alloc(size_t size, size_t align);
  static inline void free(void*);

  void* value = nullptr;
  const TypeInfo* type = nullptr;
};

inline any::~any() {
  reset();
}

template <typename T>
inline any::any(const T& val) {
  *this = val;
}

any::any(const any& other) noexcept : type(other.type) {
  if (other.value != nullptr) {
    value = alloc(type->size, type->alignment);
    type->copyConstruct(value, other.value);
  }
}

any::any(any&& other) noexcept : value(other.value), type(other.type) {
  other.value = nullptr;
  other.type = nullptr;
}

void any::reset() {
  if (value != nullptr) {
    type->destruct(value);
    free(value);
  }
  value = nullptr;
  type = nullptr;
}

any& any::operator=(const any& rhs) {
  reset();
  type = rhs.type;
  if (rhs.value != nullptr) {
    value = alloc(type->size, type->alignment);
    type->copyConstruct(value, rhs.value);
  }
  return *this;
}

any& any::operator=(any&& rhs) noexcept {
  value = rhs.value;
  type = rhs.type;
  rhs.value = nullptr;
  rhs.type = nullptr;
  return *this;
}

template <typename T>
any& any::operator=(const T& val) {
  if (!is<T>()) {
    reset();
    type = TypeOf<T>::type();
    value = alloc(type->size, type->alignment);
    type->copyConstruct(value, &val);
  } else {
    *reinterpret_cast<T*>(value) = val;
  }
  return *this;
}

template <typename T>
T& any::get() const {
  assert(is<T>());
  return *reinterpret_cast<T*>(value);
}

template <typename T>
bool any::is() const {
  return type == TypeOf<T>::type();
}

void* any::alloc(size_t size, size_t align) {
  // TODO: Use an internal buffer for small types
  auto buf = new uint8_t[size];
  // TODO: Deal with alignment.
  assert(reinterpret_cast<uintptr_t>(buf) % align == 0);
  return buf;
}

void any::free(void* ptr) {
  delete[] reinterpret_cast<uint8_t*>(ptr);
}

}  // namespace dap

#endif  // dap_any_h
