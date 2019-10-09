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

#ifndef dap_socket_h
#define dap_socket_h

#include <stdint.h>
#include <mutex>

namespace dap {

class Socket {
 public:
  Socket(const char* address, const char* port);
  ~Socket();

  bool isOpen();
  void close();

  bool listen(int backlog = 1);
  bool select(int us);
  Socket* accept();

  int receive(uint8_t* buffer, int length);
  void send(const uint8_t* buffer, int length);

 private:
  static constexpr int InvalidSocket = -1;

  Socket(int socket);
  std::mutex mutex;
  int socket = InvalidSocket;
};

}  // namespace dap

#endif  // dap_socket_h
