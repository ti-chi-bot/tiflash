// Copyright 2023 PingCAP, Inc.
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

#include <Common/ProfileEvents.h>
#include <IO/WriteBufferFromFile.h>
#include <IO/WriteHelpers.h>
#include <fcntl.h>
#include <sys/stat.h>


namespace ProfileEvents
{
extern const Event FileOpen;
extern const Event FileOpenFailed;
} // namespace ProfileEvents

namespace DB
{
namespace ErrorCodes
{
extern const int FILE_DOESNT_EXIST;
extern const int CANNOT_OPEN_FILE;
extern const int CANNOT_CLOSE_FILE;
} // namespace ErrorCodes


WriteBufferFromFile::WriteBufferFromFile(
    const std::string & file_name_,
    size_t buf_size,
    int flags,
    mode_t mode,
    char * existing_memory,
    size_t alignment)
    : WriteBufferFromFileDescriptor(-1, buf_size, existing_memory, alignment)
    , file_name(file_name_)
{
    ProfileEvents::increment(ProfileEvents::FileOpen);

#ifdef __APPLE__
    bool o_direct = (flags != -1) && (flags & O_DIRECT);
    if (o_direct)
        flags = flags & ~O_DIRECT;
#endif

    fd = open(file_name.c_str(), flags == -1 ? O_WRONLY | O_TRUNC | O_CREAT : flags, mode);

    if (-1 == fd)
    {
        ProfileEvents::increment(ProfileEvents::FileOpenFailed);
        throwFromErrno(
            "Cannot open file " + file_name,
            errno == ENOENT ? ErrorCodes::FILE_DOESNT_EXIST : ErrorCodes::CANNOT_OPEN_FILE);
    }

#ifdef __APPLE__
    if (o_direct)
    {
        if (fcntl(fd, F_NOCACHE, 1) == -1)
        {
            ProfileEvents::increment(ProfileEvents::FileOpenFailed);
            throwFromErrno("Cannot set F_NOCACHE on file " + file_name, ErrorCodes::CANNOT_OPEN_FILE);
        }
    }
#endif
}


/// Use pre-opened file descriptor.
WriteBufferFromFile::WriteBufferFromFile(
    int fd,
    const std::string & original_file_name,
    size_t buf_size,
    char * existing_memory,
    size_t alignment)
    : WriteBufferFromFileDescriptor(fd, buf_size, existing_memory, alignment)
    , file_name(original_file_name.empty() ? "(fd = " + toString(fd) + ")" : original_file_name)
{}


WriteBufferFromFile::~WriteBufferFromFile()
{
    if (fd < 0)
        return;

    try
    {
        next();
    }
    catch (...)
    {
        tryLogCurrentException(__PRETTY_FUNCTION__);
    }

    ::close(fd);
}


/// Close file before destruction of object.
void WriteBufferFromFile::close()
{
    next();

    if (0 != ::close(fd))
        throw Exception("Cannot close file", ErrorCodes::CANNOT_CLOSE_FILE);

    fd = -1;
    metric_increment.destroy();
}

} // namespace DB
