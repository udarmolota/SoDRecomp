#pragma once
// Guest path namespace -> host folders.
//
// Guest paths come in three shapes:
//   game:\packages\core.erf       (drive: symbolic link)
//   \Device\Cdrom0\default.xex    (device path)
//   \??\d:\foo                    (DOS-devices prefix + drive)
// Drives resolve through symbolic links (ObCreateSymbolicLink, XamContentCreate)
// to device paths, and device paths map onto host directories by longest prefix.

#include "objects.h"

namespace sod::kernel
{
    void VfsInit(const std::filesystem::path& gameRoot, const std::filesystem::path& userRoot);

    // Maps a guest device path (e.g. "\Device\Cdrom0") to a host directory.
    void VfsMountDevice(const std::string& devicePath, const std::filesystem::path& hostRoot);
    void VfsUnmountDevice(const std::string& devicePath);

    // "game:" -> "\Device\Cdrom0". Names are case-insensitive.
    void VfsCreateSymlink(const std::string& link, const std::string& target);
    bool VfsDeleteSymlink(const std::string& link);

    // Host folders layered over a device, highest priority first (mods over the
    // disc): a path that exists in an overlay resolves there.
    void VfsSetOverlays(const std::string& devicePath, std::vector<std::filesystem::path> overlays);
    // Serves a host file the runtime generates at a guest path, above the overlays
    // (over the disc and the mods). Only that exact path resolves to it.
    void VfsAddFile(std::string_view guestPath, const std::filesystem::path& hostPath);
    // (SoD) The Russian text. State of Decay has none of its own; a Russian title update the
    // player supplies carries a translation, which the install rebuilds on the disc's string
    // keys into French's file (languages/french.xbox360.btxt) inside a full copy of
    // game\gamedata.pak, kept next to the game folder as russian/gamedata.pak (the launcher's
    // libshaderlib.so --sod-russian). Where that copy exists, French plays in Russian: the
    // game's game\gamedata.pak is served from it. Without it French is French. The whole pak, not just the text:
    // CryPak takes a file's size from the pak it was first found in, and the game's text
    // loader rejects a file whose size it didn't expect. Call after LoadSettings.
    void VfsMountRussianText(const std::filesystem::path& gameRoot);

    // Resolves a guest path to a host path; nullopt if no device matches.
    std::optional<std::filesystem::path> VfsResolve(std::string_view guestPath);
    // Every host directory a guest directory is made of (existing overlays first,
    // then the device's own path), for merged directory listings.
    std::vector<std::filesystem::path> VfsResolveAll(std::string_view guestPath);

    // Is this host path under a read-only (disc) device?
    bool VfsIsReadOnly(const std::filesystem::path& hostPath);

    const std::filesystem::path& VfsGameRoot();
    const std::filesystem::path& VfsUserRoot();

    struct File final : KernelObject
    {
        File() : KernelObject(ObjectType::File) {}
        ~File() override;

        std::string guestPath;
        std::filesystem::path hostPath;
        HANDLE host = INVALID_HANDLE_VALUE;
        bool isDirectory = false;
        bool synchronous = true;
        bool deleteOnClose = false;
        uint64_t position = 0;
        std::mutex mutex;

        // Directory enumeration state (NtQueryDirectoryFile).
        std::vector<std::filesystem::directory_entry> entries;
        size_t nextEntry = 0;
        bool enumerated = false;
        std::string searchPattern; // from the first call: FindNextFile passes none
        bool returnedAny = false;  // since the list was made: else "no such file"

        // File handles are always signaled (I/O completes synchronously).
        bool IsWaitable() const override { return true; }
        bool IsSignaled(GuestThread*) const override { return true; }
    };
}
