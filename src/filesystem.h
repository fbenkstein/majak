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

#if __has_include(<filesystem>)
#include <filesystem>
namespace ninja {
namespace fs = std::filesystem;
}
#elif __has_include(<experimental/filesystem>)
#include <experimental/filesystem>
namespace ninja {
namespace fs = std::experimental::filesystem;
}
#else
#error no filesystem library available
#endif

#endif  // NINJA_FILESYSTEM_H_
