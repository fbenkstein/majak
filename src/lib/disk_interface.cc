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

#include <ninja/disk_interface.h>

#include <algorithm>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>  // _mkdir
#include <windows.h>
#include <sstream>
#endif

#include <ninja/metrics.h>
#include <ninja/util.h>

namespace ninja {

namespace {

std::string DirName(const std::string& path) {
#ifdef _WIN32
  const char kPathSeparators[] = "\\/";
#else
  const char kPathSeparators[] = "/";
#endif
  std::string::size_type slash_pos = path.find_last_of(kPathSeparators);
  if (slash_pos == std::string::npos)
    return std::string();  // Nothing to do.
  const char* const kEnd = kPathSeparators + strlen(kPathSeparators);
  while (slash_pos > 0 &&
         std::find(kPathSeparators, kEnd, path[slash_pos - 1]) != kEnd)
    --slash_pos;
  return path.substr(0, slash_pos);
}

int MakeDir(const std::string& path) {
#ifdef _WIN32
  return _mkdir(path.c_str());
#else
  return mkdir(path.c_str(), 0777);
#endif
}

#ifdef _WIN32
TimeStamp TimeStampFromFileTime(const FILETIME& filetime) {
  // FILETIME is in 100-nanosecond increments since the Windows epoch.
  // We don't much care about epoch correctness but we do want the
  // resulting value to fit in a 64-bit integer.
  uint64_t mtime = ((uint64_t)filetime.dwHighDateTime << 32) |
                   ((uint64_t)filetime.dwLowDateTime);
  // 1600 epoch -> 2000 epoch (subtract 400 years).
  return (TimeStamp)mtime - 12622770400LL * (1000000000LL / 100);
}

TimeStamp StatSingleFile(const std::string& path, std::string* err) {
  WIN32_FILE_ATTRIBUTE_DATA attrs;
  if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &attrs)) {
    DWORD win_err = GetLastError();
    if (win_err == ERROR_FILE_NOT_FOUND || win_err == ERROR_PATH_NOT_FOUND)
      return 0;
    *err = "GetFileAttributesEx(" + path + "): " + GetLastErrorString();
    return -1;
  }
  return TimeStampFromFileTime(attrs.ftLastWriteTime);
}
#endif  // _WIN32

}  // namespace

// DiskInterface ---------------------------------------------------------------

bool DiskInterface::MakeDirs(const std::string& path) {
  std::string dir = DirName(path);
  if (dir.empty())
    return true;  // Reached root; assume it's there.
  std::string err;
  TimeStamp mtime = Stat(dir, &err);
  if (mtime < 0) {
    Error("%s", err.c_str());
    return false;
  }
  if (mtime > 0)
    return true;  // Exists already; we're done.

  // Directory doesn't exist.  Try creating its parent first.
  bool success = MakeDirs(dir);
  if (!success)
    return false;
  return MakeDir(dir);
}

// RealDiskInterface -----------------------------------------------------------

TimeStamp RealDiskInterface::Stat(const std::string& path,
                                  std::string* err) const {
  METRIC_RECORD("node stat");
#ifdef _WIN32
  // MSDN: "Naming Files, Paths, and Namespaces"
  // http://msdn.microsoft.com/en-us/library/windows/desktop/aa365247(v=vs.85).aspx
  if (!path.empty() && path[0] != '\\' && path.size() > MAX_PATH) {
    std::ostringstream err_stream;
    err_stream << "Stat(" << path << "): Filename longer than " << MAX_PATH
               << " characters";
    *err = err_stream.str();
    return -1;
  }
  return StatSingleFile(path, err);
#else
  struct stat st;
  if (stat(path.c_str(), &st) < 0) {
    if (errno == ENOENT || errno == ENOTDIR)
      return 0;
    *err = "stat(" + path + "): " + strerror(errno);
    return -1;
  }
  // Some users (Flatpak) set mtime to 0, this should be harmless
  // and avoids conflicting with our return value of 0 meaning
  // that it doesn't exist.
  if (st.st_mtime == 0)
    return 1;
#if defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
  return ((int64_t)st.st_mtimespec.tv_sec * 1000000000LL +
          st.st_mtimespec.tv_nsec);
#elif (_POSIX_C_SOURCE >= 200809L || _XOPEN_SOURCE >= 700 ||                   \
       defined(_BSD_SOURCE) || defined(_SVID_SOURCE) || defined(__BIONIC__) || \
       (defined(__SVR4) && defined(__sun)))
  // For glibc, see "Timestamp files" in the Notes of
  // http://www.kernel.org/doc/man-pages/online/pages/man2/stat.2.html newlib,
  // uClibc and musl follow the kernel (or Cygwin) headers and define the right
  // macro values above. For bsd, see
  // https://github.com/freebsd/freebsd/blob/master/sys/sys/stat.h and similar
  // For bionic, C and POSIX API is always enabled.
  // For solaris, see
  // https://docs.oracle.com/cd/E88353_01/html/E37841/stat-2.html.
  return (int64_t)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#else
  return (int64_t)st.st_mtime * 1000000000LL + st.st_mtimensec;
#endif
#endif
}

bool RealDiskInterface::WriteFile(const std::string& path,
                                  const std::string& contents) {
  FILE* fp = fopen(path.c_str(), "w");
  if (fp == nullptr) {
    Error("WriteFile(%s): Unable to create file. %s", path.c_str(),
          strerror(errno));
    return false;
  }

  if (fwrite(contents.data(), 1, contents.length(), fp) < contents.length()) {
    Error("WriteFile(%s): Unable to write to the file. %s", path.c_str(),
          strerror(errno));
    fclose(fp);
    return false;
  }

  if (fclose(fp) == EOF) {
    Error("WriteFile(%s): Unable to close the file. %s", path.c_str(),
          strerror(errno));
    return false;
  }

  return true;
}

bool RealDiskInterface::MakeDir(const std::string& path) {
  if (::ninja::MakeDir(path) < 0) {
    if (errno == EEXIST) {
      return true;
    }
    Error("mkdir(%s): %s", path.c_str(), strerror(errno));
    return false;
  }
  return true;
}

FileReader::Status RealDiskInterface::ReadFile(const std::string& path,
                                               std::string* contents,
                                               std::string* err) {
  switch (::ninja::ReadFile(path, contents, err)) {
  case 0:
    return Okay;
  case -ENOENT:
    return NotFound;
  default:
    return OtherError;
  }
}

int RealDiskInterface::RemoveFile(const std::string& path) {
  if (remove(path.c_str()) < 0) {
    switch (errno) {
    case ENOENT:
      return 1;
    default:
      Error("remove(%s): %s", path.c_str(), strerror(errno));
      return -1;
    }
  } else {
    return 0;
  }
}

}  // namespace ninja
