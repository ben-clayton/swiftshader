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

#ifndef dap_server_h
#define dap_server_h

#include "connection.h"
#include "handlers.h"
#include "typeinfo.h"
#include "typeof.h"

#include <functional>
#include <future>

namespace dap {

class Server {
 public:
  static std::unique_ptr<Server> create(int port = 19020);

  virtual ~Server() = default;

  // begin() starts accepting debugger connections.
  virtual void begin() = 0;

  // end() terminates any existing debugger connections, and then shuts down.
  virtual void end() = 0;

  template <typename T>
  inline void registerRequestHandler(const RequestHandler<T>& handler);

  template <typename T>
  inline void registerResponseSentHandler(
      const ResponseSentHandler<T>& handler);

  template <
      typename T,
      typename = typename std::enable_if<detail::Traits<T>::isEvent>::type>
  void broadcast(const T& event);

 protected:
  virtual void registerRequestHandler(const TypeInfo* typeinfo,
                              const detail::GenericRequestHandler& handler) = 0;

  virtual void registerResponseSentHandler(
      const TypeInfo* typeinfo,
      const detail::GenericResponseSentHandler& handler) = 0;

  virtual void broadcast(const TypeInfo*, const void* event) = 0;

private:
  class Impl;
};

template <typename T>
void Server::registerRequestHandler(const RequestHandler<T>& handler) {
  using ResponseType = typename T::Response;
  auto cb = [handler](Connection* conn, const void* args,
                      const detail::RequestSuccessCallback& onSuccess,
                      const detail::RequestErrorCallback& onError) {
    auto res = handler(conn, reinterpret_cast<const T*>(args));
    if (res.error) {
      onError(res.error);
    } else {
      onSuccess(TypeOf<ResponseType>::type(), &res.response);
    }
  };
  const TypeInfo* typeinfo = TypeOf<T>::type();
  registerRequestHandler(typeinfo, cb);
}

template <typename T>
void Server::registerResponseSentHandler(
    const ResponseSentHandler<T>& handler) {
  auto cb = [handler](Connection* conn, const void* response,
                      const Error* error) {
    if (error != nullptr) {
      handler(conn, ResponseOrError<T>(*error));
    } else {
      handler(conn, ResponseOrError<T>(*reinterpret_cast<const T*>(response)));
    }
  };
  const TypeInfo* typeinfo = TypeOf<T>::type();
  registerResponseSentHandler(typeinfo, cb);
}

template <typename T, typename>
void Server::broadcast(const T& event) {
  const TypeInfo* typeinfo = TypeOf<T>::type();
  broadcast(typeinfo, &event);
}

}  // namespace dap

#endif  // dap_server_h
