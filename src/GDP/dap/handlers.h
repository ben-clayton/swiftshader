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

#ifndef dap_handlers_h
#define dap_handlers_h

#include "variant.h"

#include <functional>
#include <string>

#include <stdarg.h>
#include <stdio.h>

namespace dap {

class Connection;

struct Error {
  inline Error() = default;
  inline Error(const std::string& error);
  inline Error(const char* msg, ...);

  inline operator bool() const;

  std::string error;  // empty represents success.
};

Error::Error(const std::string& error) : error(error) {}

Error::Error(const char* msg, ...) {
  char buf[2048];
  va_list vararg;
  va_start(vararg, msg);
  vsnprintf(buf, sizeof(buf), msg, vararg);
  va_end(vararg);
  error = buf;
}

Error::operator bool() const {
  return error.size() > 0;
}

template <typename T>
struct ResponseOrError {
  inline ResponseOrError(const T& response);
  inline ResponseOrError(const Error& error);
  inline ResponseOrError(const ResponseOrError& other);

  T response;
  Error error;  // empty represents success.
};

template <typename T>
ResponseOrError<T>::ResponseOrError(const T& response) : response(response) {}
template <typename T>
ResponseOrError<T>::ResponseOrError(const Error& error) : error(error) {}
template <typename T>
ResponseOrError<T>::ResponseOrError(const ResponseOrError& other)
    : response(other.response), error(other.error) {}

template <typename T>
using RequestHandler =
    std::function<ResponseOrError<typename T::Response>(Connection* conn,
                                                        const T* args)>;

template <typename T>
using ResponseSentHandler =
    std::function<void(Connection* conn, const ResponseOrError<T>&)>;

template <typename T>
using EventHandler =
    std::function<void(Connection* conn, int sequence, const T* body)>;

namespace detail {
using RequestSuccessCallback =
    std::function<void(const TypeInfo*, const void*)>;

using RequestErrorCallback = std::function<void(const Error& message)>;

using GenericRequestHandler =
    std::function<void(Connection* conn,
                       const void* args,
                       const RequestSuccessCallback& onSuccess,
                       const RequestErrorCallback& onError)>;

using GenericResponseSentHandler = std::function<
    void(Connection* conn, const void* response, const Error* error)>;
}  // namespace detail

}  // namespace dap

#endif  // dap_handlers_h
