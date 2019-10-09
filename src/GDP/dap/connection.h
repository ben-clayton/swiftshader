
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

#ifndef dap_connection_h
#define dap_connection_h

#include "handlers.h"
#include "protocol.h"

#include <future>

namespace dap {

class Connection {
 private:
 public:
  virtual ~Connection() = default;

  template <
      typename T,
      typename = typename std::enable_if<detail::Traits<T>::isRequest>::type>
  std::future<ResponseOrError<typename T::Response>> send(const T& request);

  template <
      typename T,
      typename = typename std::enable_if<detail::Traits<T>::isEvent>::type>
  void send(const T& event);

 private:
  friend class Server;
  class Impl;

  using GenericResponseHandler = std::function<void(const void*, const Error*)>;

  virtual void send(const TypeInfo*, const void* request, const GenericResponseHandler&) = 0;
  virtual void send(const TypeInfo*, const void* event) = 0;
};

template <typename T, typename>
std::future<ResponseOrError<typename T::Response>> Connection::send(
    const T& request) {
  using Response = typename T::Response;
  auto promise = std::make_shared<std::promise<ResponseOrError<Response>>>();
  const TypeInfo* typeinfo = TypeOf<T>::type();
  send(typeinfo, &request, [=](const void* result, const Error* error) {
    if (error != nullptr) {
      promise->set_value(ResponseOrError<Response>(*error));
    } else {
      promise->set_value(ResponseOrError<Response>(*reinterpret_cast<const Response*>(result)));
    }
  });
  return promise->get_future();
}

template <typename T, typename>
void Connection::send(const T& event) {
  const TypeInfo* typeinfo = TypeOf<T>::type();
  send(typeinfo, &event);
}

}  // namespace dap

#endif  // dap_connection_h