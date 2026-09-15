#include "gta4_source_inspector.h"
#include <rex/logging.h>
#include <cstdio>
#include <exception>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>
#include <rex/crypto/sha256.h>
#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/system/util/xex2_info.h>

static int InspectUpdate(const char* path, const gta4::install::GameSourceInspection& base) {
    using rex::X_STATUS;  // Required by the SDK's X_STATUS_SUCCESS macro.
    // Diagnostic companion to gta4_installer.cpp::ValidatePatch. Reuse the
    // SDK's STFS reader, which opens package storage with "rb". No extraction
    // or write calls are made against the package or base-game image.
    rex::filesystem::StfsContainerDevice device("update:", path);
    if (!device.Initialize()) throw std::runtime_error("Cannot read update package");
    const uint32_t title = device.header().metadata.execution_info.title_id;
    std::printf("Update package title: %08X\n", title);
    auto* root = device.ResolvePath("");
    if (!root) throw std::runtime_error("Update has no readable root");
    std::vector<rex::filesystem::Entry*> pending{root};
    rex::filesystem::Entry* patch = nullptr;
    size_t visited = 0;
    while (!pending.empty()) {
        auto* entry = pending.back();
        pending.pop_back();
        if (++visited > 100000) throw std::runtime_error("Update tree exceeds inspection limit");
        if (entry->name() == "default.xexp") {
            if (patch) throw std::runtime_error("Multiple default.xexp entries");
            patch = entry;
        }
        for (const auto& child : entry->children()) pending.push_back(child.get());
    }
    if (!patch || patch->size() < sizeof(rex::xex2_header) || patch->size() > 64 * 1024 * 1024)
        throw std::runtime_error("Missing or invalid-sized default.xexp");
    rex::filesystem::File* raw = nullptr;
    if (patch->Open(rex::filesystem::FileAccess::kGenericRead, &raw) != X_STATUS_SUCCESS || !raw)
        throw std::runtime_error("Cannot open default.xexp read-only");
    auto destroy = [](rex::filesystem::File* file) { file->Destroy(); };
    std::unique_ptr<rex::filesystem::File, decltype(destroy)> file(raw, destroy);
    std::vector<uint8_t> bytes(patch->size());
    size_t read = 0;
    if (file->ReadSync(bytes, 0, &read) != X_STATUS_SUCCESS || read != bytes.size())
        throw std::runtime_error("Incomplete default.xexp read");
    if (std::memcmp(bytes.data(), "XEX2", 4) != 0)
        throw std::runtime_error("Patch is not XEX2");
    const auto* header = reinterpret_cast<const rex::xex2_header*>(bytes.data());
    const size_t header_size = header->header_size;
    const size_t count = header->header_count;
    if (header_size < sizeof(*header) || header_size > bytes.size() || count > 4096 ||
        count > (header_size - offsetof(rex::xex2_header, headers)) / sizeof(rex::xex2_opt_header))
        throw std::runtime_error("Malformed patch optional-header table");
    const rex::xex2_opt_delta_patch_descriptor* delta = nullptr;
    for (size_t i = 0; i < count; ++i) {
        if (header->headers[i].key != rex::XEX_HEADER_DELTA_PATCH_DESCRIPTOR) continue;
        const size_t offset = header->headers[i].offset;
        if (delta || offset > header_size ||
            offsetof(rex::xex2_opt_delta_patch_descriptor, info) > header_size - offset)
            throw std::runtime_error("Malformed/duplicate delta descriptor");
        delta = reinterpret_cast<const rex::xex2_opt_delta_patch_descriptor*>(bytes.data() + offset);
    }
    if (!delta) throw std::runtime_error("No delta descriptor");
    const auto hash = rex::crypto::sha256(std::string_view(
        reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    const uint32_t source = delta->source_version_value;
    const uint32_t target = delta->target_version_value;
    const bool signature_matches = std::memcmp(delta->digest_source,
        base.rsa_signature_sha1.data(), base.rsa_signature_sha1.size()) == 0;
    std::printf("Patch bytes: %zu\nPatch SHA-256: %s\nSource version: %s\nTarget version: %s\n",
        bytes.size(), hash.c_str(), gta4::install::FormatXexVersion(source).c_str(),
        gta4::install::FormatXexVersion(target).c_str());
    std::printf("Patch source signature: ");
    for (const auto byte : delta->digest_source) std::printf("%02x", byte);
    std::printf("\nBase executable signature match: %s\n", signature_matches ? "yes" : "NO");
    // Pin from the existing installer, not from the supplied package/filename.
    constexpr auto expected_hash = "480aee5e2b42707791e7571bb8407c5bb3f6c7534f07f9beb426db4cfc648fd3";
    const uint32_t flags = header->module_flags;
    const uint32_t expected_flags = rex::XEX_MODULE_MODULE_PATCH | rex::XEX_MODULE_PATCH_DELTA;
    const bool supported = base.supported() && title == base.title_id &&
        (flags & expected_flags) == expected_flags && source == base.xex_version &&
        target == 0x00000805 && hash == expected_hash && signature_matches;
    std::puts(supported ? "Update compatibility: SUPPORTED" :
        "Update compatibility: REJECTED (does not match this base/build's required patch)");
    return supported ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) {
        std::fprintf(stderr, "Usage: theft4_inspect <Xbox 360 ISO or extracted game directory> [title-update package]\n");
        return 2;
    }
    rex::InitLogging();
    int result = 2;
    try {
        // Reuse the upstream installer identity checks and read-only mounts.
        // Never patch, extract over, or modify the original user-owned dump.
        const auto inspection = gta4::install::InspectGameSource(argv[1]);
        std::puts(gta4::install::FormatGameSourceDiagnostics(inspection).c_str());
        std::puts(gta4::install::FormatGameSourceInspection(inspection).c_str());
        result = inspection.supported() ? 0 : 1;
        if (argc == 3 && inspection.supported()) result = InspectUpdate(argv[2], inspection);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Inspection failed: %s\n", error.what());
        result = 2;
    }
    rex::ShutdownLogging();
    return result;
}
