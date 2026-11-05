#pragma once
// The host file behind a guest File (kernel/objects.h: `host`): a Win32 HANDLE on Windows,
// a POSIX descriptor kept in the same field elsewhere. Positioned reads and writes, as
// NtReadFile/NtWriteFile need them; errors are the host's codes, for the log.
#include <filesystem>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace sod::kernel::hostfile
{
    enum class Creation { Supersede, Open, Create, OpenIf, Overwrite, OverwriteIf };

#ifdef _WIN32
    inline HANDLE Open(const std::filesystem::path& path, bool write, Creation creation, uint32_t& error)
    {
        DWORD how = OPEN_EXISTING;
        switch (creation)
        {
        case Creation::Supersede: how = CREATE_ALWAYS; break;
        case Creation::Open: how = OPEN_EXISTING; break;
        case Creation::Create: how = CREATE_NEW; break;
        case Creation::OpenIf: how = OPEN_ALWAYS; break;
        case Creation::Overwrite: how = TRUNCATE_EXISTING; break;
        case Creation::OverwriteIf: how = CREATE_ALWAYS; break;
        }
        const HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | (write ? GENERIC_WRITE : 0),
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, how, FILE_ATTRIBUTE_NORMAL, nullptr);
        error = h == INVALID_HANDLE_VALUE ? GetLastError() : 0;
        return h;
    }

    inline bool IsAccessDenied(uint32_t error)
    {
        return error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION;
    }

    // Reads `want` bytes at `at`; the end of the file isn't an error. 0, or the host's error.
    inline uint32_t Read(HANDLE h, void* to, uint32_t want, uint64_t at, uint32_t& got)
    {
        OVERLAPPED ov{};
        ov.Offset = DWORD(at);
        ov.OffsetHigh = DWORD(at >> 32);
        DWORD read = 0;
        const bool ok = ReadFile(h, to, want, &read, &ov);
        got = read;
        if (ok)
            return ERROR_SUCCESS;
        const DWORD error = GetLastError();
        return error == ERROR_HANDLE_EOF ? DWORD(ERROR_SUCCESS) : error;
    }

    inline bool Write(HANDLE h, const void* from, uint32_t length, uint64_t at, uint32_t& written)
    {
        OVERLAPPED ov{};
        ov.Offset = DWORD(at);
        ov.OffsetHigh = DWORD(at >> 32);
        DWORD n = 0;
        const bool ok = WriteFile(h, from, length, &n, &ov);
        written = n;
        return ok;
    }

    inline void Flush(HANDLE h) { FlushFileBuffers(h); }

    inline bool SetSize(HANDLE h, uint64_t size)
    {
        FILE_END_OF_FILE_INFO eof{};
        eof.EndOfFile.QuadPart = int64_t(size);
        return SetFileInformationByHandle(h, FileEndOfFileInfo, &eof, sizeof(eof));
    }

    inline void Close(HANDLE h) { ::CloseHandle(h); }
#else
    inline int Fd(HANDLE h) { return int(reinterpret_cast<intptr_t>(h)); }

    inline HANDLE Open(const std::filesystem::path& path, bool write, Creation creation, uint32_t& error)
    {
        int flags = (write ? O_RDWR : O_RDONLY) | O_CLOEXEC;
        switch (creation)
        {
        case Creation::Supersede: flags |= O_CREAT | O_TRUNC; break;
        case Creation::Open: break;
        case Creation::Create: flags |= O_CREAT | O_EXCL; break;
        case Creation::OpenIf: flags |= O_CREAT; break;
        case Creation::Overwrite: flags |= O_TRUNC; break;
        case Creation::OverwriteIf: flags |= O_CREAT | O_TRUNC; break;
        }
        const int fd = open(path.c_str(), flags, 0644);
        error = fd < 0 ? uint32_t(errno) : 0;
        return fd < 0 ? INVALID_HANDLE_VALUE : reinterpret_cast<HANDLE>(intptr_t(fd));
    }

    inline bool IsAccessDenied(uint32_t error) { return error == EACCES || error == EPERM || error == EROFS; }

    inline uint32_t Read(HANDLE h, void* to, uint32_t want, uint64_t at, uint32_t& got)
    {
        got = 0;
        auto* out = static_cast<uint8_t*>(to);
        while (got < want)
        {
            const ssize_t n = pread(Fd(h), out + got, want - got, off_t(at + got));
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                return uint32_t(errno);
            }
            if (n == 0)
                break;
            got += uint32_t(n);
        }
        return 0;
    }

    inline bool Write(HANDLE h, const void* from, uint32_t length, uint64_t at, uint32_t& written)
    {
        written = 0;
        const auto* in = static_cast<const uint8_t*>(from);
        while (written < length)
        {
            const ssize_t n = pwrite(Fd(h), in + written, length - written, off_t(at + written));
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }
            written += uint32_t(n);
        }
        return true;
    }

    inline void Flush(HANDLE h) { fsync(Fd(h)); }
    inline bool SetSize(HANDLE h, uint64_t size) { return ftruncate(Fd(h), off_t(size)) == 0; }
    inline void Close(HANDLE h) { close(Fd(h)); }
#endif
}
