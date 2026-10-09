#include "theft4_mod_mount.h"
#include <rex/filesystem/devices/host_path_entry.h>
#include <cassert>
#include <chrono>
#include <iostream>

using namespace rex::filesystem;
using rex::X_STATUS;
static std::string Read(const std::filesystem::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), {}};
}
static std::filesystem::path HostPath(VirtualFileSystem& fs, std::string_view path) {
    auto* entry = dynamic_cast<HostPathEntry*>(fs.ResolvePath(path));
    assert(entry);
    return entry->host_path();
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::filesystem::path mod(argv[1]);
    assert(theft4::mods::ValidTimeCycleFile(mod / "timecyc.dat"));
    const auto bytes = Read(mod / "timecyc.dat");
    for (const std::string invalid : {bytes.substr(0, bytes.size() / 2),
             bytes + "1e", bytes + "nan", bytes + "0", bytes + bytes,
             std::string("// comments only\n"), std::string("1.0garbage\n")}) {
        std::istringstream input(invalid);
        assert(!theft4::mods::ValidTimeCycle(input));
    }
    std::istringstream comments(bytes + "\n // trailing comment\r\n\t");
    assert(theft4::mods::ValidTimeCycle(comments));
    const auto root = std::filesystem::temp_directory_path() / ("theft4-mod-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto data = root / "XBOX360" / "DATA";
    std::filesystem::create_directories(data);
    std::ofstream(data / "TIMECYC.DAT") << "original";
    std::ofstream(data / "timecyc.dat.bak") << "backup";
    std::ofstream(data / "weather.dat") << "weather";
    {
        VirtualFileSystem fs;
        auto device = std::make_unique<HostPathDevice>("\\Device\\Harddisk0\\Partition1", root, true);
        assert(device->Initialize());
        fs.RegisterDevice(std::move(device));
        fs.RegisterSymbolicLink("game:", "\\Device\\Harddisk0\\Partition1");
        fs.RegisterSymbolicLink("d:", "\\Device\\Harddisk0\\Partition1");
        // Off: normal VFS reads the original; no mod device or scan exists.
        assert(Read(HostPath(fs, "game:/xbox360/data/timecyc.dat")) == "original");
        bool rejected = false;
        try { theft4::mods::MountTimeCycle(fs, root / "missing"); }
        catch (const std::runtime_error&) { rejected = true; }
        assert(rejected);
        assert(Read(HostPath(fs, "d:/xbox360/data/timecyc.dat")) == "original");
        theft4::mods::MountTimeCycle(fs, mod);
        // The title creates these aliases after the mod is mounted.
        fs.RegisterSymbolicLink("platform:", "\\Device\\Harddisk0\\Partition1\\xbox360");
        fs.RegisterSymbolicLink("xbox360:", "\\Device\\Harddisk0\\Partition1\\xbox360");
        for (const auto path : {"game:/xbox360/data/timecyc.dat", "D:\\XBOX360\\DATA\\TIMECYC.DAT",
                               "platform:/data/timecyc.dat", "xbox360:/data/timecyc.dat"}) {
            assert(Read(HostPath(fs, path)) == bytes);
            File* file = nullptr;
            FileAction action;
            assert(fs.OpenFile(nullptr, path, FileDisposition::kOpen, FileAccess::kGenericRead,
                               false, true, &file, &action) == X_STATUS_SUCCESS);
            assert(file && action == FileAction::kOpened);
            file->Destroy();
            file = nullptr;
            // The runtime intentionally downgrades write-open requests on
            // read-only devices to read access for guest compatibility.
            assert(fs.OpenFile(nullptr, path, FileDisposition::kOpen, FileAccess::kGenericWrite,
                               false, true, &file, &action) == X_STATUS_SUCCESS);
            assert(file && !(file->file_access() & (FileAccess::kFileWriteData | FileAccess::kFileAppendData)));
            file->Destroy();
            file = nullptr;
            assert(fs.OpenFile(nullptr, path, FileDisposition::kOverwrite, FileAccess::kGenericWrite,
                               false, true, &file, &action) == X_STATUS_ACCESS_DENIED);
        }
        assert(Read(HostPath(fs, "platform:/data/weather.dat")) == "weather");
        assert(Read(HostPath(fs, "platform:/data/timecyc.dat.bak")) == "backup");
        assert(!fs.ResolvePath("platform:/data/timecyc.dat.not-a-file"));
    }
    // A fresh runtime with the mod Off restores the original without copying it.
    {
        VirtualFileSystem fs;
        auto device = std::make_unique<HostPathDevice>("\\Device\\Harddisk0\\Partition1", root, true);
        assert(device->Initialize());
        fs.RegisterDevice(std::move(device));
        fs.RegisterSymbolicLink("game:", "\\Device\\Harddisk0\\Partition1");
        assert(Read(HostPath(fs, "game:/xbox360/data/timecyc.dat")) == "original");
    }
    assert(Read(data / "TIMECYC.DAT") == "original");
    assert(Read(mod / "timecyc.dat") == bytes);
    std::filesystem::remove_all(root);
    std::cout << "Time-cycle validation, alias routing, read-only opens, sibling preservation and On/Off restoration passed.\n";
}
