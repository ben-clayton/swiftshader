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

#include "server.h"

#include "any.h"
#include "chan.h"
#include "json_serializer.h"
#include "socket.h"

#include <stdarg.h>
#include <stdio.h>
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#define FAIL(msg, ...) fail(msg "\n", ##__VA_ARGS__)

namespace {

struct MessageHandlers {
  std::unordered_map<
      std::string,
      std::pair<const dap::TypeInfo*, dap::detail::GenericRequestHandler>>
      request;
  std::unordered_map<const dap::TypeInfo*,
                     dap::detail::GenericResponseSentHandler>
      responseSent;
};

void fail(const char* msg, ...) {
  va_list vararg;
  va_start(vararg, msg);
  vfprintf(stderr, msg, vararg);
  va_end(vararg);

  ::abort();
}

}  // anonymous namespace

namespace dap {

class Server::Impl : public Server {
 public:
  Impl(int port);
  ~Impl();

  void begin() override;
  void end() override;

  void registerRequestHandler(
      const TypeInfo* typeinfo,
      const detail::GenericRequestHandler& handler) override;

  void registerResponseSentHandler(
      const TypeInfo* typeinfo,
      const detail::GenericResponseSentHandler& handler) override;

  void broadcast(const TypeInfo* typeinfo, const void* event) override;

 private:
  std::unique_ptr<Connection::Impl> accept();

  int const port;
  std::thread thread;
  std::mutex mutex;
  std::atomic<bool> shutdown = {false};
  std::vector<std::unique_ptr<Connection::Impl>> connections;
  MessageHandlers handlers;
};

class Connection::Impl : public Connection {
 public:
  Impl(Socket*, const MessageHandlers&);
  ~Impl();

  void begin();
  void end();

  void send(const TypeInfo*,
            const void* request,
            const GenericResponseHandler&) override;
  void send(const TypeInfo*, const void* event) override;

 private:
  using Payload = std::function<void()>;
  struct ResponseHandler {
    GenericResponseHandler handler;
    const TypeInfo* responseType;
  };

  bool scan(const uint8_t* seq, size_t len);
  bool scan(const char* str);
  bool match(const uint8_t* seq, size_t len);
  bool match(const char* str);
  char matchAny(const char* chars);
  bool buffer(size_t bytes);

  std::string parseMessage();
  Payload processMessage(const std::string& str);
  Payload processRequest(const nlohmann::json& json, int sequence);
  void processResponse(const nlohmann::json& json);
  void send(const nlohmann::json& msg);

  std::unique_ptr<Socket> socket;
  MessageHandlers handlers;
  std::thread recvThread;
  std::thread dispatchThread;
  Chan<Payload> inbox;
  std::deque<uint8_t> buf;
  std::mutex sendMutex;
  int nextSeq = 1;  // guarded by sendMutex
  std::unordered_map<int, ResponseHandler> responseHandlers;
};

Server::Impl::Impl(int port) : port(port) {}

Server::Impl::~Impl() {
  end();
}

void Server::Impl::begin() {
  end();
  shutdown = false;
  thread = std::thread([this] {
    char strPort[16];
    snprintf(strPort, sizeof(strPort), "%d", port);
    Socket socket("localhost", strPort);
    if (!socket.listen()) {
      FAIL("Unable to listen on port '%d'", port);
      return;
    }

    while (!shutdown) {
      if (socket.select(1000000)) {
        if (auto s = socket.accept()) {
          auto connection = std::unique_ptr<Connection::Impl>(
              new Connection::Impl(s, handlers));
          connection->begin();
          std::unique_lock<std::mutex> lock(mutex);
          connections.emplace_back(std::move(connection));
        }
      }
    }

    for (auto& connection : connections) {
      connection->end();
    }
    connections.clear();
  });
}

void Server::Impl::end() {
  if (thread.joinable()) {
    shutdown = true;
    thread.join();
  }
}

void Server::Impl::registerRequestHandler(
    const TypeInfo* typeinfo,
    const detail::GenericRequestHandler& handler) {
  std::unique_lock<std::mutex> lock(mutex);
  handlers.request.emplace(
      std::make_pair(typeinfo->name, std::make_pair(typeinfo, handler)));
}

void Server::Impl::registerResponseSentHandler(
    const TypeInfo* typeinfo,
    const detail::GenericResponseSentHandler& handler) {
  std::unique_lock<std::mutex> lock(mutex);
  handlers.responseSent.emplace(typeinfo, handler);
}

void Server::Impl::broadcast(const TypeInfo* typeinfo, const void* event) {
  std::unique_lock<std::mutex> lock(mutex);
  for (auto const& c : connections) {
    c->send(typeinfo, event);
  }
}

Connection::Impl::Impl(Socket* socket, const MessageHandlers& handlers)
    : socket(socket), handlers(handlers) {}

Connection::Impl::~Impl() {
  end();
  socket.reset();
}

void Connection::Impl::begin() {
  end();
  inbox.reset();
  recvThread = std::thread([this] {
    while (socket->isOpen()) {
      auto request = parseMessage();
      if (request.size() > 0) {
        if (auto payload = processMessage(request)) {
          inbox.put(std::move(payload));
        }
      }
    }
  });
  dispatchThread = std::thread([this] {
    Payload payload;
    while (inbox.take(payload)) {
      payload();
    }
  });
}

void Connection::Impl::end() {
  inbox.close();
  if (recvThread.joinable()) {
    socket->close();
    recvThread.join();
  }
  if (dispatchThread.joinable()) {
    dispatchThread.join();
  }
}

bool Connection::Impl::scan(const uint8_t* seq, size_t len) {
  while (buffer(len)) {
    if (match(seq, len)) {
      return true;
    }
    buf.pop_front();
  }
  return false;
}

bool Connection::Impl::scan(const char* str) {
  auto len = strlen(str);
  return scan(reinterpret_cast<const uint8_t*>(str), len);
}

bool Connection::Impl::match(const uint8_t* seq, size_t len) {
  if (!buffer(len)) {
    return false;
  }
  auto it = buf.begin();
  for (size_t i = 0; i < len; i++, it++) {
    if (*it != seq[i]) {
      return false;
    }
  }
  for (size_t i = 0; i < len; i++) {
    buf.pop_front();
  }
  return true;
}

bool Connection::Impl::match(const char* str) {
  auto len = strlen(str);
  return match(reinterpret_cast<const uint8_t*>(str), len);
}

char Connection::Impl::matchAny(const char* chars) {
  if (!buffer(1)) {
    return false;
  }
  int c = buf.front();
  if (auto p = strchr(chars, c)) {
    buf.pop_front();
    return *p;
  }
  return 0;
}

bool Connection::Impl::buffer(size_t bytes) {
  if (bytes < buf.size()) {
    return true;
  }
  bytes -= buf.size();
  while (bytes > 0) {
    uint8_t chunk[256];
    auto c = std::min(sizeof(chunk), bytes);
    if (socket->receive(chunk, c) <= 0) {
      return false;
    }
    for (size_t i = 0; i < c; i++) {
      buf.push_back(chunk[i]);
    }
    bytes -= c;
  }
  return true;
}

std::string Connection::Impl::parseMessage() {
  // Find Content-Length header prefix
  if (!scan("Content-Length:")) {
    return "";
  }
  // Skip whitespace and tabs
  while (matchAny(" \t")) {
  }
  // Parse length
  size_t len = 0;
  while (true) {
    auto c = matchAny("0123456789");
    if (c == 0) {
      break;
    }
    len *= 10;
    len += c - '0';
  }
  if (len == 0) {
    return "";
  }
  // Expect \r\n\r\n
  if (!match("\r\n\r\n")) {
    return "";
  }
  // Read message
  if (!buffer(len)) {
    return "";
  }
  std::string out;
  out.reserve(len);
  for (size_t i = 0; i < len; i++) {
    out.push_back(static_cast<char>(buf.front()));
    buf.pop_front();
  }
  return out;
}

Connection::Impl::Payload Connection::Impl::processMessage(
    const std::string& str) {
  auto json = nlohmann::json::parse(str);
  if (json.is_discarded()) {
    return {};
  }
  auto const& typeIt = json.find("type");
  if (typeIt == json.end() || !typeIt->is_string()) {
    FAIL("Message missing string 'type' field");
    return {};
  }
  auto type = typeIt->get<std::string>();

  auto const& sequenceIt = json.find("seq");
  if (sequenceIt == json.end() || !sequenceIt->is_number_integer()) {
    FAIL("Message missing number 'seq' field");
    return {};
  }
  auto sequence = sequenceIt->get<int>();

  if (type == "request") {
    return processRequest(json, sequence);
  } else if (type == "event") {
    FAIL("TODO: Event handling");
  } else if (type == "response") {
    processResponse(json);
    return {};
  } else {
    FAIL("Unknown type '%s'", type.c_str());
  }

  return {};
}

Connection::Impl::Payload Connection::Impl::processRequest(
    const nlohmann::json& json,
    int sequence) {
  auto const& commandIt = json.find("command");
  if (commandIt == json.end() || !commandIt->is_string()) {
    FAIL("Request missing string 'command' field");
    return {};
  }
  auto command = commandIt->get<std::string>();

  auto reqIt = handlers.request.find(command);
  if (reqIt == handlers.request.end()) {
    FAIL("No request handler registered for command '%s'", command.c_str());
    return {};
  }

  auto const typeinfo = reqIt->second.first;
  auto const handler = reqIt->second.second;
  auto data = new uint8_t[typeinfo->size];
  typeinfo->construct(data);

  bool ok = true;

  auto const& argumentsIt = json.find("arguments");
  if (argumentsIt != json.end()) {
    if (argumentsIt->is_object()) {
      JSONDeserializer deserializer{*argumentsIt};
      if (!typeinfo->deserialize(&deserializer, data)) {
        FAIL("Failed to deserialize request");
        ok = false;
      }
    } else {
      FAIL("Request 'arguments' field is not an object");
      ok = false;
    }
  }

  if (!ok) {
    typeinfo->destruct(data);
    delete[] data;
    return {};
  }

  return [=] {
    handler(this, data,
            [&](const TypeInfo* typeinfo, const void* data) {
              // onSuccess
              nlohmann::json message;
              message["type"] = "response";
              message["request_seq"] = sequence;
              message["success"] = true;
              message["command"] = command;
              JSONSerializer serializer(message["body"]);
              typeinfo->serialize(&serializer, data);
              send(message);

              auto sentIt = handlers.responseSent.find(typeinfo);
              if (sentIt != handlers.responseSent.end()) {
                sentIt->second(this, data, nullptr);
              }
            },
            [&](const Error& error) {
              // onError
              nlohmann::json message;
              message["type"] = "response";
              message["request_seq"] = sequence;
              message["success"] = false;
              message["command"] = command;
              message["message"] = error.error;
              send(message);

              auto sentIt = handlers.responseSent.find(typeinfo);
              if (sentIt != handlers.responseSent.end()) {
                sentIt->second(this, nullptr, &error);
              }
            });
    typeinfo->destruct(data);
    delete[] data;
  };
}

void Connection::Impl::processResponse(const nlohmann::json& json) {
  auto const& requestSeqIt = json.find("request_seq");
  if (requestSeqIt == json.end() || !requestSeqIt->is_number_integer()) {
    FAIL("Response missing int 'request_seq' field");
    return;
  }
  auto requestSeq = requestSeqIt->get<int>();

  auto responseIt = responseHandlers.find(requestSeq);
  if (responseIt == responseHandlers.end()) {
    FAIL("Unknown response with sequence %d", requestSeq);
    return;
  }

  auto responseHandler = responseIt->second;

  auto const& successIt = json.find("success");
  if (successIt == json.end() || !successIt->is_boolean()) {
    FAIL("Response missing boolean 'success' field");
    return;
  }
  auto success = successIt->get<bool>();

  if (success) {
    auto const typeinfo = responseHandler.responseType;
    auto data = std::unique_ptr<uint8_t[]>(new uint8_t[typeinfo->size]);

    typeinfo->construct(data.get());

    auto const& bodyIt = json.find("body");
    if (bodyIt != json.end() && bodyIt->is_object()) {
      JSONDeserializer deserializer{*bodyIt};
      if (typeinfo->deserialize(&deserializer, data.get())) {
        responseHandler.handler(data.get(), nullptr);
      } else {
        FAIL("Failed to deserialize request");
      }
    } else {
      FAIL("Request 'body' field is not an object");
    }

    typeinfo->destruct(data.get());
  } else {
    std::string message;
    auto const& messageIt = json.find("message");
    if (messageIt != json.end() && messageIt->is_string()) {
      message = messageIt->get<string>();
    }
    auto error = Error("%s", message.c_str());
    responseHandler.handler(nullptr, &error);
  }
}

void Connection::Impl::send(const nlohmann::json& msg) {
  std::unique_lock<std::mutex> lock(sendMutex);

  auto msgWithSeq = msg;
  msgWithSeq["seq"] = nextSeq++;
  auto str = msgWithSeq.dump();

  auto header =
      std::string("Content-Length: ") + std::to_string(str.size()) + "\r\n\r\n";
  socket->send(reinterpret_cast<const uint8_t*>(header.data()), header.size());
  socket->send(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

void Connection::Impl::send(const TypeInfo* typeinfo,
                            const void* request,
                            const GenericResponseHandler& responseHandler) {
  // TODO: Make this all thread-safe.
  responseHandlers[nextSeq] = ResponseHandler{responseHandler, typeinfo};

  nlohmann::json message;
  message["type"] = "request";
  message["command"] = typeinfo->name;
  JSONSerializer serializer(message["arguments"]);
  typeinfo->serialize(&serializer, request);
  send(message);
}

void Connection::Impl::send(const TypeInfo* typeinfo, const void* event) {
  nlohmann::json message;
  message["type"] = "event";
  message["event"] = typeinfo->name;
  JSONSerializer serializer(message["body"]);
  typeinfo->serialize(&serializer, event);
  send(message);
}

std::unique_ptr<Server> Server::create(int port /* = 19020 */) {
  return std::unique_ptr<Server>(new Impl(port));
}

}  // namespace dap
