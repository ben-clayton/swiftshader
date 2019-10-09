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

#include "socket.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#if defined(_WIN32)
#include <atomic>
namespace {
std::atomic<int> wsaInitCount = {0};
}  // anonymous namespace
#endif

namespace dap {

Socket::Socket(const char* address, const char* port) {
#if defined(_WIN32)
  if (wsaInitCount++ == 0) {
    WSADATA winsockData;
    WSAStartup(MAKEWORD(2, 2), &winsockData);
  }
#endif

  addrinfo hints = {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;

  addrinfo* info = 0;
  getaddrinfo(address, port, &hints, &info);

  if (info) {
    socket = ::socket(info->ai_family, info->ai_socktype, info->ai_protocol);
    bind(socket, info->ai_addr, (int)info->ai_addrlen);
  }
}

Socket::Socket(int socket) : socket(socket) {
#if defined(_WIN32)
  if (wsaInitCount++ == 0) {
    WSADATA winsockData;
    WSAStartup(MAKEWORD(2, 2), &winsockData);
  }
#endif
}

Socket::~Socket() {
  close();
#if defined(_WIN32)
  if (--wsaInitCount == 0) {
    WSACleanup();
  }
#endif

}

bool Socket::isOpen() {
  std::unique_lock<std::mutex> lock(mutex);
  if (socket == InvalidSocket) {
    return false;
  }

  char error = 0;
  socklen_t len = sizeof(error);
  getsockopt(socket, SOL_SOCKET, SO_ERROR, &error, &len);
  if (error != 0) {
    socket = InvalidSocket;
    return false;
  }

  return true;
}

void Socket::close() {
  std::unique_lock<std::mutex> lock(mutex);
  if (socket != InvalidSocket) {
#if defined(_WIN32)
    closesocket(socket);
#else
    ::shutdown(socket, SHUT_RDWR);
    ::close(socket);
#endif
    socket = InvalidSocket;
  }
}

bool Socket::listen(int backlog) {
  std::unique_lock<std::mutex> lock(mutex);
  auto s = socket;
  lock.unlock();
  if (s == InvalidSocket) {
    return false;
  }

  return ::listen(s, backlog) == 0;
}

bool Socket::select(int us) {
  std::unique_lock<std::mutex> lock(mutex);
  auto s = socket;
  lock.unlock();
  if (s == InvalidSocket) {
    return false;
  }

  fd_set sockets;
  FD_ZERO(&sockets);
  FD_SET(s, &sockets);

  timeval timeout = {us / 1000000, us % 1000000};

  return ::select(FD_SETSIZE, &sockets, 0, 0, &timeout) >= 1;
}

Socket* Socket::accept() {
  std::unique_lock<std::mutex> lock(mutex);
  auto s = socket;
  lock.unlock();
  return (s != InvalidSocket) ? new Socket(::accept(socket, 0, 0)) : nullptr;
}

int Socket::receive(uint8_t* buffer, int length) {
  std::unique_lock<std::mutex> lock(mutex);
  auto s = socket;
  lock.unlock();
  return (s != InvalidSocket) ? recv(s, reinterpret_cast<char*>(buffer), length, 0) : -1;
}

void Socket::send(const uint8_t* buffer, int length) {
  std::unique_lock<std::mutex> lock(mutex);
  auto s = socket;
  lock.unlock();
  if (s != InvalidSocket) {
    ::send(s, reinterpret_cast<const char*>(buffer), length, 0);
  }
}


}  // namespace dap
