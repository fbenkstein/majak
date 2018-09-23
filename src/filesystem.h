// Copyright 2018 Frank Benkstein
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

#ifndef NINJA_FILESYSTEM_H_
#define NINJA_FILESYSTEM_H_

#include <functional>

#include "ninja_config.h"

#define NINJA_FILESYSTEM_INCLUDE_ <NINJA_FILESYSTEM_INCLUDE>
#include NINJA_FILESYSTEM_INCLUDE_

namespace ninja::fs {
using namespace NINJA_FILESYSTEM_NAMESPACE;

namespace detail {
template <class T, class U>
auto __get_error_code_type_helper(T (*f)(U&)) {
  return U{};
}
}  // namespace detail

using error_code =
    decltype(detail::__get_error_code_type_helper(&temp_directory_path));
}  // namespace ninja::fs

#endif  // NINJA_FILESYSTEM_H_
