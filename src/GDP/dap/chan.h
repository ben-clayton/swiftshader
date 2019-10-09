
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

#ifndef dap_chan_h
#define dap_chan_h

#include <condition_variable>
#include <mutex>
#include <queue>

namespace dap {

template <typename T>
struct Chan {
 public:
  void reset();
  void close();
  bool take(T& out);
  void put(T&& in);

 private:
  bool closed;
  std::queue<T> queue;
  std::condition_variable cv;
  std::mutex mutex;
};

template <typename T>
void Chan<T>::reset() {
  std::unique_lock<std::mutex> lock(mutex);
  queue = {};
  closed = false;
}

template <typename T>
void Chan<T>::close() {
  std::unique_lock<std::mutex> lock(mutex);
  closed = true;
  cv.notify_all();
}

template <typename T>
bool Chan<T>::take(T& out) {
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [&] { return queue.size() > 0 || closed; });
  if (closed) {
    return false;
  }
  out = queue.front();
  queue.pop();
  return true;
}

template <typename T>
void Chan<T>::put(T&& in) {
  std::unique_lock<std::mutex> lock(mutex);
  auto notify = queue.size() == 0 && !closed;
  queue.push(in);
  if (notify) {
    cv.notify_all();
  }
}

}  // namespace dap

#endif  // dap_chan_h