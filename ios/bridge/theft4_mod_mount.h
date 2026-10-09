#pragma once
#include "theft4_timecycle_mod.h"
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/string.h>

namespace theft4::mods {
// VFS symlinks are prefix-based. Preserve files whose names merely begin with
// timecyc.dat (for example timecyc.dat.bak) instead of redirecting those too.
class TimeCycleDevice final : public rex::filesystem::HostPathDevice {
public:
    TimeCycleDevice(const std::filesystem::path& bundleDirectory,
                    rex::filesystem::Entry* originalDirectory)
        : HostPathDevice("\\Device\\Theft4TimeCycle", bundleDirectory, true),
          originalDirectory_(originalDirectory) {}

    rex::filesystem::Entry* ResolvePath(std::string_view path) override {
        const auto name = rex::string::utf8_canonicalize_guest_path(path);
        if (rex::string::utf8_equal_case(name, "timecyc.dat") ||
            rex::string::utf8_equal_case(name, "\\timecyc.dat"))
            return HostPathDevice::ResolvePath(path);
        return originalDirectory_->ResolvePath(path);
    }
private:
    rex::filesystem::Entry* originalDirectory_;
};

class TimeCycleArchiveDevice final : public rex::filesystem::HostPathDevice {
public:
    TimeCycleArchiveDevice(const std::filesystem::path& archive,
                          rex::filesystem::Entry* originalRoot)
        : HostPathDevice("\\Device\\Theft4PackedTimeCycle", archive.parent_path(), true),
          archiveName_(archive.filename().string()), originalRoot_(originalRoot) {}
    rex::filesystem::Entry* ResolvePath(std::string_view path) override {
        const auto name = rex::string::utf8_canonicalize_guest_path(path);
        if (rex::string::utf8_equal_case(name, "xbox360.rpf") ||
            rex::string::utf8_equal_case(name, "\\xbox360.rpf"))
            return HostPathDevice::ResolvePath(archiveName_);
        return originalRoot_->ResolvePath(path);
    }
private:
    std::string archiveName_;
    rex::filesystem::Entry* originalRoot_;
};

inline void MountTimeCycle(rex::filesystem::VirtualFileSystem& fs,
                           const std::filesystem::path& bundleDirectory,
                           const std::filesystem::path& preparedArchive = {}) {
    if (!ValidTimeCycleFile(bundleDirectory / "timecyc.dat"))
        throw std::runtime_error("Custom Time Cycle is missing or invalid. Turn it off in Mods to use the original.");
    auto* originalDirectory = fs.ResolvePath("game:\\xbox360\\data");
    if (!originalDirectory || !originalDirectory->GetChild("timecyc.dat"))
        throw std::runtime_error("Custom Time Cycle requires the base game's xbox360/data/timecyc.dat. Turn it off in Mods to continue.");
    auto device = std::make_unique<TimeCycleDevice>(bundleDirectory, originalDirectory);
    if (!device->Initialize() || !device->ResolvePath("timecyc.dat"))
        throw std::runtime_error("Could not open Custom Time Cycle. Turn it off in Mods to continue.");
    fs.RegisterDevice(std::move(device));
    // Redirect the physical guest path: game:, d:, platform: and xbox360:
    // aliases all converge here, including aliases created by the title later.
    fs.RegisterSymbolicLink("\\Device\\Harddisk0\\Partition1\\xbox360\\data\\timecyc.dat",
                            "\\Device\\Theft4TimeCycle\\timecyc.dat");
    if (!preparedArchive.empty()) {
        auto* root = fs.ResolvePath("game:");
        if (!root || !root->GetChild("xbox360.rpf"))
            throw std::runtime_error("Custom Time Cycle: original archive is unavailable.");
        auto archiveDevice = std::make_unique<TimeCycleArchiveDevice>(preparedArchive, root);
        if (!archiveDevice->Initialize() || !archiveDevice->ResolvePath("xbox360.rpf"))
            throw std::runtime_error("Custom Time Cycle: prepared archive is unavailable.");
        fs.RegisterDevice(std::move(archiveDevice));
        fs.RegisterSymbolicLink("\\Device\\Harddisk0\\Partition1\\xbox360.rpf",
                                "\\Device\\Theft4PackedTimeCycle\\xbox360.rpf");
    }
}
} // namespace theft4::mods
