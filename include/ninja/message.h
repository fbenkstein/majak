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

#ifndef NINJA_MESSAGE_H_
#define NINJA_MESSAGE_H_

#include <absl/strings/str_format.h>

#include <utility>

namespace ninja {

enum class MessageType {
  kFatal,
  kError,
  kWarning,
  kExplain,
};

/// Log a message with the given type. With kFatal this function doesn't return.
void Message(MessageType type, std::string_view message);

/// Log a fatal message and exit.
template <class... Args>
[[noreturn]] void Fatal(const absl::FormatSpec<Args...>& format,
                        const Args&... args) {
  Message(MessageType::kFatal, absl::StrFormat(format, args...));
  std::abort();  // unreachable
}

/// Log an error message.
template <class... Args>
void Error(const absl::FormatSpec<Args...>& format, const Args&... args) {
  Message(MessageType::kError, absl::StrFormat(format, args...));
}

/// Log a warning message.
template <class... Args>
void Warning(const absl::FormatSpec<Args...>& format, const Args&... args) {
  Message(MessageType::kWarning, absl::StrFormat(format, args...));
}

}  // namespace ninja

#endif  // NINJA_MESSAGE_H_
