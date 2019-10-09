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

#ifndef dap_optional_h
#define dap_optional_h

#include "typeinfo.h"

#include <assert.h>

namespace dap {

// optional is an exception-free reimplementation of C++ 17's std::optional
// that can used in C++ 11.
template <typename T>
class optional {
 public:
  using value_type = T;

  inline optional() = default;
  inline optional(const optional& other);
  inline optional(optional&& other);
  template <typename U>
  inline optional(const optional<U>& other);
  template <typename U>
  inline optional(optional<U>&& other);
  template <typename U = value_type>
  inline optional(U&& value);

  inline T& value();
  inline const T& value() const;

  inline T& value(const T& defaultValue);
  inline const T& value(const T& defaultValue) const;

  inline bool has_value() const;

  inline optional& operator=(const optional& other);
  inline optional& operator=(optional&& other) noexcept;

  template <typename U = T>
  inline optional& operator=(U&& value);

  template <typename U>
  inline optional& operator=(const optional<U>& other);

  template <typename U>
  inline optional& operator=(optional<U>&& other);

  inline const T* operator->() const;
  inline T* operator->();
  inline const T& operator*() const&;
  inline T& operator*() &;
  inline const T&& operator*() const&&;
  inline T&& operator*() &&;

 private:
  T val = {};
  bool set = false;
};

template <typename T>
optional<T>::optional(const optional& other) : val(other.val), set(other.set) {}

template <typename T>
optional<T>::optional(optional&& other)
    : val(std::move(other.val)), set(other.set) {}

template <typename T>
template <typename U>
optional<T>::optional(const optional<U>& other)
    : val(other.val), set(other.set) {}

template <typename T>
template <typename U>
optional<T>::optional(optional<U>&& other)
    : val(std::move(other.val)), set(other.set) {}

template <typename T>
template <typename U /*= T*/>
optional<T>::optional(U&& value) : val(std::move(value)), set(true) {}

template <typename T>
T& optional<T>::value() {
  assert(set);
  return val;
}

template <typename T>
const T& optional<T>::value() const {
  assert(set);
  return val;
}

template <typename T>
T& optional<T>::value(const T& defaultValue) {
  if (!has_value()) {
    return defaultValue;
  }
  return val;
}

template <typename T>
const T& optional<T>::value(const T& defaultValue) const {
  if (!has_value()) {
    return defaultValue;
  }
  return val;
}

template <typename T>
bool optional<T>::has_value() const {
  return set;
}

template <typename T>
optional<T>& optional<T>::operator=(const optional& other) {
  val = other.val;
  set = other.set;
  return *this;
}

template <typename T>
optional<T>& optional<T>::operator=(optional&& other) noexcept {
  val = std::move(other.val);
  set = other.set;
  return *this;
}

template <typename T>
template <typename U /* = T */>
optional<T>& optional<T>::operator=(U&& value) {
  val = std::move(value);
  set = true;
  return *this;
}

template <typename T>
template <typename U>
optional<T>& optional<T>::operator=(const optional<U>& other) {
  val = other.val;
  set = other.set;
  return *this;
}

template <typename T>
template <typename U>
optional<T>& optional<T>::operator=(optional<U>&& other) {
  val = std::move(other.val);
  set = other.set;
  return *this;
}

template <typename T>
const T* optional<T>::operator->() const {
  assert(set);
  return &val;
}

template <typename T>
T* optional<T>::operator->() {
  assert(set);
  return &val;
}

template <typename T>
const T& optional<T>::operator*() const& {
  assert(set);
  return val;
}

template <typename T>
T& optional<T>::operator*() & {
  assert(set);
  return val;
}

template <typename T>
const T&& optional<T>::operator*() const&& {
  assert(set);
  return std::move(val);
}

template <typename T>
T&& optional<T>::operator*() && {
  assert(set);
  return std::move(val);
}

}  // namespace dap

#endif  // dap_optional_h
