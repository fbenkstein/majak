// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <ninja/message.h>

#include <cstdio>
#include <cstdlib>

namespace ninja {
void Message(MessageType type, std::string_view message) {
  switch (type) {
  case MessageType::kFatal:
    std::fputs("ninja: fatal: ", stderr);
    break;
  case MessageType::kError:
    std::fputs("ninja: error: ", stderr);
    break;
  case MessageType::kWarning:
    std::fputs("ninja: warning: ", stderr);
    break;
  case MessageType::kExplain:
    std::fputs("ninja explain: ", stderr);
    break;
  }

  std::fwrite(message.data(), 1, message.size(), stderr);
  std::fputc('\n', stderr);

  if (type == MessageType::kFatal) {
#ifdef _WIN32
    // On Windows, some tools may inject extra threads.
    // exit() may block on locks held by those threads, so forcibly exit.
    std::fflush(stderr);
    std::fflush(stdout);
    std::_Exit(1);
#else
    std::exit(1);
#endif
  }
}
}  // namespace ninja
