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

#include <ninja/ninja_config.h>

#include <functional>

#include <boost/filesystem.hpp>

namespace ninja::fs {
using namespace boost::filesystem;
using error_code = boost::system::error_code;
}  // namespace ninja::fs

#endif  // NINJA_FILESYSTEM_H_