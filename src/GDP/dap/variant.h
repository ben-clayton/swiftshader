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
// WITHOUT WARRANTIES OR CONDITIONS OF variant KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef dap_variant_h
#define dap_variant_h

#include "any.h"

namespace dap {

template <typename T0, typename... Types>
class variant {
 public:
  inline variant();

  template <typename T>
  inline variant(const T& val);

  template <typename T>
  inline variant& operator=(const T& val);

  template <typename T>
  inline T& get() const;

  template <typename T>
  inline bool is() const;

  any value;  // TODO: Hide this.
};

template <typename T0, typename... Types>
variant<T0, Types...>::variant() : value(T0()) {}

template <typename T0, typename... Types>
template <typename T>
variant<T0, Types...>::variant(const T& value) : value(value) {
  // TODO: assert T is in <T0, Types...>
}

template <typename T0, typename... Types>
template <typename T>
variant<T0, Types...>& variant<T0, Types...>::operator=(const T& value) {
  return *this;
}

template <typename T0, typename... Types>
template <typename T>
T& variant<T0, Types...>::get() const {
  return value.get<T>();
}

template <typename T0, typename... Types>
template <typename T>
bool variant<T0, Types...>::is() const {
  return value.is<T>();
}

}  // namespace dap

#endif  // dap_variant_h
