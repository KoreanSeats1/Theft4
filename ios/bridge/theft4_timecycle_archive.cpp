#include "theft4_timecycle_archive.h"
#include "theft4_timecycle_mod.h"
#include <xxhash.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <span>
#include <vector>
extern "C" {
#include <rijndael-alg-fst.h>
}

namespace theft4::mods {
namespace {
using Bytes = std::vector<uint8_t>;
constexpr uint64_t kTocOffset = 0x800;
uint32_t Get(std::span<const uint8_t> bytes, size_t p) {
    if (p > bytes.size() || bytes.size() - p < 4)
        throw std::runtime_error("Custom Time Cycle: truncated archive table.");
    uint32_t v; std::memcpy(&v, bytes.data() + p, 4); return v;
}
void Put(Bytes& bytes, size_t p, uint32_t value) {
    std::memcpy(bytes.data() + p, &value, 4);
}
Bytes Read(const std::filesystem::path& path, uint64_t offset, size_t length) {
    std::ifstream file(path, std::ios::binary); file.seekg(std::streamoff(offset));
    Bytes bytes(length);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(length)))
        throw std::runtime_error("Custom Time Cycle: cannot read archive data.");
    return bytes;
}
void Crypt(Bytes& bytes, const Bytes& key, bool encrypt) {
    if (key.size() != 32 || bytes.size() % 16)
        throw std::runtime_error("Custom Time Cycle: encrypted archive requires a valid AES key.");
    std::array<uint32_t, 60> schedule{};
    const int rounds = encrypt ? rijndaelKeySetupEnc(schedule.data(), key.data(), 256)
                               : rijndaelKeySetupDec(schedule.data(), key.data(), 256);
    std::array<uint8_t,16> block{};
    for (unsigned pass=0; pass<16; ++pass)
        for (size_t p=0; p<bytes.size(); p+=16) {
            if (encrypt) rijndaelEncrypt(schedule.data(), rounds, bytes.data()+p, block.data());
            else rijndaelDecrypt(schedule.data(), rounds, bytes.data()+p, block.data());
            std::memcpy(bytes.data()+p, block.data(), 16);
        }
}
size_t TimeCycleEntry(const Bytes& toc, uint32_t count, uint64_t source_size) {
    const size_t names = size_t(count) * 16;
    std::vector<bool> visited(count);
    size_t found = SIZE_MAX;
    auto name = [&](uint32_t i) {
        const uint64_t p = uint64_t(names) + Get(toc, size_t(i)*16);
        if (p >= toc.size()) throw std::runtime_error("Custom Time Cycle: invalid archive name.");
        std::string s;
        for (size_t j=size_t(p); j<toc.size() && s.size()<=255; ++j) {
            const auto c=toc[j];
            if (!c) return s;
            if (c<32 || c>126 || c=='/' || c=='\\') break;
            s += char(std::tolower(c));
        }
        throw std::runtime_error("Custom Time Cycle: invalid archive name.");
    };
    std::function<void(uint32_t, std::string, unsigned)> visit =
      [&](uint32_t i, std::string path, unsigned depth) {
        if (i>=count || depth>32 || visited[i])
            throw std::runtime_error("Custom Time Cycle: invalid archive directory graph.");
        visited[i]=true;
        if (i) path += (path.empty() ? "" : "/") + name(i);
        const size_t p=size_t(i)*16;
        const uint32_t offset=Get(toc,p+8), flags=Get(toc,p+12);
        if (offset & 0x80000000) {
            const uint32_t first=offset&0x7fffffff, length=flags&0x0fffffff;
            if (uint64_t(first)+length>count)
                throw std::runtime_error("Custom Time Cycle: archive children are out of bounds.");
            for(uint32_t n=0;n<length;++n) visit(first+n,path,depth+1);
        } else if (path=="data/timecyc.dat") {
            const uint64_t stored=flags&0xbfffffff;
            if (found!=SIZE_MAX || (flags&0xc0000000)==0xc0000000 ||
                offset<kTocOffset+toc.size() || offset>source_size || stored>source_size-offset)
                throw std::runtime_error("Custom Time Cycle: invalid or duplicate archive table entry.");
            found=p;
        }
      };
    if (!(Get(toc,8)&0x80000000))
        throw std::runtime_error("Custom Time Cycle: archive root is not a directory.");
    visit(0,"",0);
    if(found==SIZE_MAX)
        throw std::runtime_error("Custom Time Cycle: xbox360.rpf has no data/timecyc.dat.");
    return found;
}
}

std::filesystem::path PrepareTimeCycleArchive(const std::filesystem::path& game,
    const std::filesystem::path& bundle, const std::filesystem::path& cache) {
    const auto source=game/"xbox360.rpf", mod=bundle/"timecyc.dat";
    if (!std::filesystem::exists(source)) return {};
    if (!ValidTimeCycleFile(mod))
        throw std::runtime_error("Custom Time Cycle is missing or invalid. Turn it off in Mods.");
    const uint64_t size=std::filesystem::file_size(source);
    const auto stamp=std::filesystem::last_write_time(source).time_since_epoch().count();
    if(size<kTocOffset+16 || size>1024ull*1024*1024)
        throw std::runtime_error("Custom Time Cycle: unsupported archive size.");
    const auto header=Read(source,0,20);
    const uint32_t toc_size=Get(header,4), count=Get(header,8);
    if(Get(header,0)!=0x32465052 || !count || count>1000000 ||
       toc_size<uint64_t(count)*16 || toc_size>32*1024*1024 || toc_size>size-kTocOffset)
        throw std::runtime_error("Custom Time Cycle: invalid RPF2 archive header.");
    auto original_toc=Read(source,kTocOffset,toc_size);
    const bool encrypted=Get(header,16)!=0;
    Bytes key;
    if(encrypted) {
        const auto key_path=game/"aes_key.bin";
        if(!std::filesystem::exists(key_path) || std::filesystem::file_size(key_path)!=32)
            throw std::runtime_error("Custom Time Cycle: archive AES key is missing or invalid.");
        key=Read(key_path,0,32);
    }
    auto toc=original_toc;
    if(encrypted) Crypt(toc,key,false);
    const size_t entry=TimeCycleEntry(toc,count,size);
    const auto replacement=Read(mod,0,size_t(std::filesystem::file_size(mod)));
    const uint64_t replacement_offset=(size+2047)&~uint64_t(2047);
    Put(toc,entry+4,uint32_t(replacement.size()));
    Put(toc,entry+8,uint32_t(replacement_offset));
    Put(toc,entry+12,uint32_t(replacement.size())); // ordinary, uncompressed data
    auto encoded=toc;
    if(encrypted) Crypt(encoded,key,true);

    // Small metadata reads only. Cache reuse validates the patched table and
    // exact replacement bytes; normal game I/O uses a regular read-only file.
    Bytes identity=header;
    identity.insert(identity.end(),original_toc.begin(),original_toc.end());
    identity.insert(identity.end(),replacement.begin(),replacement.end());
    const auto path=std::filesystem::absolute(source).string();
    identity.insert(identity.end(),path.begin(),path.end());
    for(auto value : {size,uint64_t(stamp),uint64_t(1)})
        for(unsigned shift=0;shift<64;shift+=8) identity.push_back(uint8_t(value>>shift));
    std::filesystem::create_directories(cache);
    const auto output=cache/("timecycle-"+std::to_string(XXH3_64bits(identity.data(),identity.size()))+".rpf");
    const uint64_t output_size=replacement_offset+replacement.size();
    auto valid = [&] {
        std::error_code ec;
        if(std::filesystem::file_size(output,ec)!=output_size || ec) return false;
        try { return Read(output,0,20)==header && Read(output,kTocOffset,toc_size)==encoded &&
                     Read(output,replacement_offset,replacement.size())==replacement; }
        catch(const std::exception&) { return false; }
    };
    if(valid()) return output;
    const auto temporary=output.string()+".tmp";
    try {
        std::ifstream input(source,std::ios::binary);
        std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
        std::vector<char> buffer(1024*1024);
        uint64_t remaining=size;
        while(remaining) {
            const auto chunk=std::streamsize(std::min<uint64_t>(remaining,buffer.size()));
            if(!input.read(buffer.data(),chunk) || !file.write(buffer.data(),chunk))
                throw std::runtime_error("Custom Time Cycle: cannot prepare private archive.");
            remaining-=uint64_t(chunk);
        }
        file.seekp(std::streamoff(kTocOffset));
        file.write(reinterpret_cast<const char*>(encoded.data()),encoded.size());
        file.seekp(std::streamoff(replacement_offset));
        file.write(reinterpret_cast<const char*>(replacement.data()),replacement.size());
        file.close();
        if(!file || std::filesystem::file_size(source)!=size ||
           std::filesystem::last_write_time(source).time_since_epoch().count()!=stamp)
            throw std::runtime_error("Custom Time Cycle: archive changed or preparation failed.");
        std::filesystem::rename(temporary,output);
        if(!valid()) throw std::runtime_error("Custom Time Cycle: private archive validation failed.");
    } catch (...) {
        std::error_code ignored; std::filesystem::remove(temporary,ignored); throw;
    }
    return output;
}
}
