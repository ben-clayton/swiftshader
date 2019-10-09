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

#ifndef dap_typeof_h
#define dap_typeof_h

#include "any.h"
#include "optional.h"
#include "typeinfo.h"
#include "variant.h"

#include <unordered_map>
#include <vector>

namespace dap {

using string = std::string;

class boolean {
 public:
  inline boolean() : val(false) {}
  inline boolean(bool i) : val(i) {}
  inline operator bool() const { return val; }
  inline boolean& operator=(bool i) {
    val = i;
    return *this;
  }

 private:
  bool val;
};

class integer {
 public:
  inline integer() : val(0) {}
  inline integer(int i) : val(i) {}
  inline operator int() const { return val; }
  inline integer& operator=(int i) {
    val = i;
    return *this;
  }

 private:
  int val;
};

class number {
 public:
  inline number() : val(0.0) {}
  inline number(double i) : val(i) {}
  inline operator double() const { return val; }
  inline number& operator=(int i) {
    val = i;
    return *this;
  }

 private:
  double val;
};

template <typename T>
using array = std::vector<T>;

template <typename K, typename V>
using map = std::unordered_map<K, V>;

struct null {};

template <typename T>
struct TypeOf {};

struct Field {
  std::string name;
  ptrdiff_t offset;
  const TypeInfo* type;
};

class Deserializer {
 public:
  virtual bool deserialize(boolean*) = 0;
  virtual bool deserialize(integer*) = 0;
  virtual bool deserialize(number*) = 0;
  virtual bool deserialize(string*) = 0;
  virtual bool deserialize(any*) = 0;
  virtual size_t count() = 0;
  virtual bool element(size_t i, const std::function<bool(Deserializer*)>&) = 0;
  virtual bool field(const std::string& name,
                     const std::function<bool(Deserializer*)>&) = 0;

  template <typename T>
  inline bool deserialize(T*);

  template <typename T>
  inline bool deserialize(dap::array<T>*);

  template <typename K, typename V>
  inline bool deserialize(dap::map<K, V>*);

  inline bool deserialize(void* object, const std::initializer_list<Field>&);
};

template <typename T>
bool Deserializer::deserialize(T* ptr) {
  return TypeOf<T>::type()->deserialize(this, ptr);
}

template <typename T>
bool Deserializer::deserialize(dap::array<T>* vec) {
  auto n = count();
  vec->resize(n);
  for (size_t i = 0; i < n; i++) {
    if (!element(i,
                 [&](Deserializer* d) { return d->deserialize(&(*vec)[i]); })) {
      return false;
    }
  }
  return true;
}

template <typename K, typename V>
bool Deserializer::deserialize(dap::map<K, V>* map) {
  assert(false);  // TODO
  return true;
}

bool Deserializer::deserialize(void* object,
                               const std::initializer_list<Field>& fields) {
  for (auto const& f : fields) {
    auto ok = field(f.name, [&](Deserializer* d) {
      auto ptr = reinterpret_cast<uint8_t*>(object) + f.offset;
      return f.type->deserialize(d, ptr);
    });
    if (!ok) {
      return false;
    }
  }
  return true;
}

class Serializer {
 public:
  virtual bool serialize(boolean) = 0;
  virtual bool serialize(integer) = 0;
  virtual bool serialize(number) = 0;
  virtual bool serialize(const string&) = 0;
  virtual bool serialize(const any&) = 0;
  virtual bool array(size_t count,
                     const std::function<bool(size_t i, Serializer*)>&) = 0;
  virtual bool field(const std::string& name,
                     const std::function<bool(Serializer*)>&) = 0;
  virtual void remove() = 0;

  template <typename T>
  inline bool serialize(const T&);

  template <typename T>
  inline bool serialize(const dap::array<T>&);

  template <typename K, typename V>
  inline bool serialize(const dap::map<K, V>&);

  inline bool serialize(const void* object,
                        const std::initializer_list<Field>&);
};

template <typename T>
bool Serializer::serialize(const T& object) {
  return TypeOf<T>::type()->serialize(this, &object);
}

template <typename T>
bool Serializer::serialize(const dap::array<T>& vec) {
  return array(vec.size(),
               [&](size_t i, Serializer* s) { return s->serialize(vec[i]); });
}

template <typename K, typename V>
bool Serializer::serialize(const dap::map<K, V>& map) {
  assert(false);  // TODO
  return true;
}

bool Serializer::serialize(const void* object,
                           const std::initializer_list<Field>& fields) {
  for (auto const& f : fields) {
    auto ok = field(f.name, [&](Serializer* d) {
      auto ptr = reinterpret_cast<const uint8_t*>(object) + f.offset;
      return f.type->serialize(d, ptr);
    });
    if (!ok) {
      return false;
    }
  }
  return true;
}

template <>
struct TypeOf<boolean> {
  static inline const TypeInfo* type() {
    static TypeInfo out = {
        "boolean",
        sizeof(boolean),
        alignof(boolean),
        [](void* ptr) { new (ptr) boolean(); },
        [](void* dst, const void* src) {
          new (dst) boolean(*reinterpret_cast<const boolean*>(src));
        },
        [](void* ptr) { reinterpret_cast<boolean*>(ptr)->~boolean(); },
        [](Deserializer* d, void* ptr) {
          boolean v = false;
          bool res = d->deserialize(&v);
          *reinterpret_cast<boolean*>(ptr) = v;
          return res;
        },
        [](Serializer* s, const void* ptr) {
          return s->serialize(*reinterpret_cast<const boolean*>(ptr));
        },
    };
    return &out;
  }
};

template <>
struct TypeOf<string> {
  static inline const TypeInfo* type() {
    static TypeInfo out = {
        "string",
        sizeof(string),
        alignof(string),
        [](void* ptr) { new (ptr) string(); },
        [](void* dst, const void* src) {
          new (dst) string(*reinterpret_cast<const string*>(src));
        },
        [](void* ptr) { reinterpret_cast<string*>(ptr)->~string(); },
        [](Deserializer* d, void* ptr) {
          string v;
          bool res = d->deserialize(&v);
          *reinterpret_cast<string*>(ptr) = v;
          return res;
        },
        [](Serializer* s, const void* ptr) {
          return s->serialize(*reinterpret_cast<const string*>(ptr));
        },
    };
    return &out;
  }
};

template <>
struct TypeOf<integer> {
  static inline const TypeInfo* type() {
    static TypeInfo out = {
        "integer",
        sizeof(integer),
        alignof(integer),
        [](void* ptr) { new (ptr) integer(); },
        [](void* dst, const void* src) {
          new (dst) integer(*reinterpret_cast<const integer*>(src));
        },
        [](void* ptr) {},
        [](Deserializer* d, void* ptr) {
          integer v = 0;
          bool res = d->deserialize(&v);
          *reinterpret_cast<integer*>(ptr) = v;
          return res;
        },
        [](Serializer* s, const void* ptr) {
          return s->serialize(*reinterpret_cast<const integer*>(ptr));
        },
    };
    return &out;
  }
};

template <>
struct TypeOf<number> {
  static inline const TypeInfo* type() {
    static TypeInfo out = {
        "number",
        sizeof(number),
        alignof(number),
        [](void* ptr) { new (ptr) number(); },
        [](void* dst, const void* src) {
          new (dst) number(*reinterpret_cast<const number*>(src));
        },
        [](void* ptr) { reinterpret_cast<number*>(ptr)->~number(); },
        [](Deserializer* d, void* ptr) {
          number v;
          bool res = d->deserialize(&v);
          *reinterpret_cast<number*>(ptr) = v;
          return res;
        },
        [](Serializer* s, const void* ptr) {
          return s->serialize(*reinterpret_cast<const number*>(ptr));
        },
    };
    return &out;
  }
};

template <>
struct TypeOf<any> {
  static inline const TypeInfo* type() {
    static TypeInfo out = {
        "any",
        sizeof(any),
        alignof(any),
        [](void* ptr) { new (ptr) any(); },
        [](void* dst, const void* src) {
          new (dst) any(*reinterpret_cast<const any*>(src));
        },
        [](void* ptr) { reinterpret_cast<any*>(ptr)->~any(); },
        [](Deserializer* d, void* ptr) {
          return d->deserialize(reinterpret_cast<any*>(ptr));
        },
        [](Serializer* s, const void* ptr) {
          return s->serialize(*reinterpret_cast<const any*>(ptr));
        },
    };
    return &out;
  }
};

template <typename T>
struct TypeOf<array<T>> {
  static inline const TypeInfo* type() {
    using Arr = array<T>;
    static TypeInfo out = {
        "array<" + TypeOf<T>::type()->name + ">",
        sizeof(Arr),
        alignof(Arr),
        [](void* ptr) { new (ptr) Arr(); },
        [](void* dst, const void* src) {
          new (dst) Arr(*reinterpret_cast<const Arr*>(src));
        },
        [](void* ptr) { reinterpret_cast<Arr*>(ptr)->~Arr(); },
        [](Deserializer* d, void* ptr) {
          return d->deserialize(reinterpret_cast<Arr*>(ptr));
        },
        [](Serializer* s, const void* ptr) {
          return s->serialize(*reinterpret_cast<const Arr*>(ptr));
        },
    };
    return &out;
  }
};

template <typename K, typename V>
struct TypeOf<map<K, V>> {
  static inline const TypeInfo* type() {
    using Map = map<K, V>;
    static TypeInfo out = {
        "map<" + TypeOf<K>::type()->name + ", " + TypeOf<V>::type()->name + ">",
        sizeof(Map),
        alignof(Map),
        [](void* ptr) { new (ptr) Map(); },
        [](void* dst, const void* src) {
          new (dst) Map(*reinterpret_cast<const Map*>(src));
        },
        [](void* ptr) { reinterpret_cast<Map*>(ptr)->~Map(); },
        [](Deserializer* d, void* ptr) {
          return d->deserialize(reinterpret_cast<Map*>(ptr));
        },
        [](Serializer* s, const void* ptr) {
          return s->serialize(*reinterpret_cast<const Map*>(ptr));
        },
    };
    return &out;
  }
};

template <typename T0, typename... Types>
struct TypeOf<variant<T0, Types...>> {
  static inline const TypeInfo* type() {
    using Ty = variant<T0, Types...>;
    static TypeInfo out = {
        "variant",
        sizeof(Ty),
        alignof(Ty),
        [](void* ptr) { new (ptr) Ty(); },
        [](void* dst, const void* src) {
          new (dst) Ty(*reinterpret_cast<const Ty*>(src));
        },
        [](void* ptr) { reinterpret_cast<Ty*>(ptr)->~Ty(); },
        [](Deserializer* d, void* ptr) {
          return d->deserialize(&reinterpret_cast<Ty*>(ptr)->value);
        },
        [](Serializer* s, const void* ptr) {
          return s->serialize(reinterpret_cast<const Ty*>(ptr)->value);
        },
    };
    return &out;
  }
};

template <typename T>
struct TypeOf<optional<T>> {
  static inline const TypeInfo* type() {
    static TypeInfo out = {
        "optional<" + TypeOf<T>::type()->name + ">",
        sizeof(optional<T>),
        alignof(optional<T>),
        [](void* ptr) { new (ptr) optional<T>(); },
        [](void* dst, const void* src) {
          new (dst) optional<T>(*reinterpret_cast<const optional<T>*>(src));
        },
        [](void* ptr) { reinterpret_cast<optional<T>*>(ptr)->~optional(); },
        [](Deserializer* d, void* ptr) {
          T v;
          if (d->deserialize(&v)) {
            *reinterpret_cast<optional<T>*>(ptr) = v;
          };
          return true;
        },
        [](Serializer* s, const void* ptr) {
          auto& v = *reinterpret_cast<const optional<T>*>(ptr);
          if (!v.has_value()) {
            s->remove();
            return true;
          }
          return s->serialize(v.value());
        },
    };
    return &out;
  }
};

// DAP_OFFSETOF() macro is a generalization of the offsetof() macro defined in
// <cstddef>. It evaluates to the offset of the given field, with fewer
// restrictions than offsetof(). We cast the address '32' and subtract it again,
// because null-dereference is undefined behavior.
#define DAP_OFFSETOF(s, m) \
  ((int)(size_t) & reinterpret_cast<const volatile char&>((((s*)32)->m)) - 32)

namespace detail {
template <class T, class M>
M member_type(M T::*);
}  // namespace detail

// DAP_TYPEOF() returns the type of the struct (s) member (m).
#define DAP_TYPEOF(s, m) decltype(detail::member_type(&s::m))

// DAP_FIELD() registers a structure field for the DAP_STRUCT_TYPEINFO macro.
#define DAP_FIELD(FIELD, NAME)                       \
  dap::Field {                                       \
    NAME, DAP_OFFSETOF(StructTy, FIELD),             \
        TypeOf<DAP_TYPEOF(StructTy, FIELD)>::type(), \
  }

#define DAP_DECLARE_STRUCT_TYPEINFO(STRUCT) \
  template <>                               \
  struct TypeOf<STRUCT> {                   \
    static const TypeInfo* type();          \
  }

#define DAP_IMPLEMENT_STRUCT_TYPEINFO(STRUCT, NAME, ...)                  \
  const TypeInfo* TypeOf<STRUCT>::type() {                                \
    using StructTy = STRUCT;                                              \
    static TypeInfo out = {                                               \
        NAME,                                                             \
        sizeof(StructTy),                                                 \
        alignof(StructTy),                                                \
        [](void* ptr) { new (ptr) StructTy(); },                          \
        [](void* dst, const void* src) {                                  \
          new (dst) StructTy(*reinterpret_cast<const StructTy*>(src));    \
        },                                                                \
        [](void* ptr) { reinterpret_cast<StructTy*>(ptr)->~StructTy(); }, \
        [](Deserializer* d, void* ptr) {                                  \
          return d->deserialize(ptr, {__VA_ARGS__});                      \
        },                                                                \
        [](Serializer* s, const void* ptr) {                              \
          return s->serialize(ptr, {__VA_ARGS__});                        \
        },                                                                \
    };                                                                    \
    return &out;                                                          \
  }

#define DAP_STRUCT_TYPEINFO(STRUCT, NAME, ...) \
  DAP_DECLARE_STRUCT_TYPEINFO(STRUCT);         \
  DAP_IMPLEMENT_STRUCT_TYPEINFO(STRUCT, NAME, __VA_ARGS__)

}  // namespace dap

#endif  // dap_typeof_h
