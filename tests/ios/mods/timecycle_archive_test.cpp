#include "theft4_timecycle_archive.h"
#include "theft4_mod_mount.h"
#include <rex/filesystem/devices/host_path_entry.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>
extern "C" {
#include <rijndael-alg-fst.h>
}
using Bytes=std::vector<uint8_t>;
static Bytes read(const std::filesystem::path& p) {
    std::ifstream s(p,std::ios::binary); return {std::istreambuf_iterator<char>(s),{}};
}
static void write(const std::filesystem::path& p,const Bytes& b) {
    std::ofstream s(p,std::ios::binary);s.write(reinterpret_cast<const char*>(b.data()),b.size());assert(s);
}
static uint32_t get(const Bytes& b,size_t p) {uint32_t v;std::memcpy(&v,b.data()+p,4);return v;}
static void put(Bytes& b,size_t p,uint32_t v) {std::memcpy(b.data()+p,&v,4);}
static void crypt(Bytes& b,const Bytes& key,bool encrypt) {
    std::array<uint32_t,60> rk{};std::array<uint8_t,16> block{};
    int rounds=encrypt?rijndaelKeySetupEnc(rk.data(),key.data(),256):rijndaelKeySetupDec(rk.data(),key.data(),256);
    // Independently exercise every block through all 16 passes.
    for(size_t p=0;p<b.size();p+=16) for(unsigned n=0;n<16;++n) {
        if(encrypt)rijndaelEncrypt(rk.data(),rounds,b.data()+p,block.data());
        else rijndaelDecrypt(rk.data(),rounds,b.data()+p,block.data());
        std::memcpy(b.data()+p,block.data(),16);
    }
}
static Bytes table(const Bytes& archive,const Bytes& key) {
    Bytes b(archive.begin()+0x800,archive.begin()+0x800+get(archive,4));
    if(get(archive,16))crypt(b,key,false);return b;
}
static Bytes fixture(bool encrypted,const Bytes& key) {
    Bytes b(8192),t(512);size_t name=7*16;
    const std::array<const char*,7> names={"root","DATA","other","weather.dat","TIMECYC.DAT","timecyc.dat.bak","timecyc.dat"};
    for(unsigned i=0;i<7;++i) {
        put(t,i*16,uint32_t(name-7*16));std::memcpy(t.data()+name,names[i],std::strlen(names[i])+1);name+=std::strlen(names[i])+1;
        if(i>=3) {put(t,i*16+4,32);put(t,i*16+8,4096+i*64);put(t,i*16+12,32);std::fill_n(b.begin()+4096+i*64,32,uint8_t(i));}
    }
    put(t,8,0x80000001);put(t,12,3);
    put(t,1*16+8,0x80000004);put(t,1*16+12,2);
    put(t,2*16+8,0x80000006);put(t,2*16+12,1);
    put(b,0,0x32465052);put(b,4,512);put(b,8,7);put(b,16,encrypted?0xffffffff:0);
    if(encrypted)crypt(t,key,true);std::copy(t.begin(),t.end(),b.begin()+0x800);return b;
}
static void verify(const Bytes& original,const Bytes& patched,const Bytes& key,const Bytes& mod) {
    const auto a=table(original,key),b=table(patched,key);size_t changed=0;
    for(size_t i=0;i<a.size();++i)if(a[i]!=b[i]) {assert(i>=4*16+4 && i<5*16);++changed;}
    assert(changed);assert(get(b,4*16+4)==mod.size());assert(get(b,4*16+12)==mod.size());
    const auto offset=get(b,4*16+8);assert(offset>=original.size());assert(offset%2048==0);
    assert(std::equal(mod.begin(),mod.end(),patched.begin()+offset));
    for(size_t i=0;i<original.size();++i)if(i<0x800 || i>=0x800+a.size())assert(original[i]==patched[i]);
}
int main(int argc,char**argv) {
    assert(argc>=2);const std::filesystem::path bundle=argv[1];const auto mod=read(bundle/"timecyc.dat");
    const auto root=std::filesystem::temp_directory_path()/("theft4-rpf-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Bytes key(32);for(unsigned i=0;i<32;++i)key[i]=uint8_t(i);
    for(bool encrypted : {false,true}) {
        const auto game=root/(encrypted?"encrypted":"plain"),cache=game/"cache";
        std::filesystem::create_directories(game/"xbox360/data");
        write(game/"aes_key.bin",key);write(game/"xbox360/data/timecyc.dat",{'s','t','o','c','k'});
        write(game/"xbox360.rpf.bak",{'b','a','k'});
        const auto original=fixture(encrypted,key);write(game/"xbox360.rpf",original);
        auto result=theft4::mods::PrepareTimeCycleArchive(game,bundle,cache);verify(original,read(result),key,mod);
        const auto modified=std::filesystem::last_write_time(result);
        assert(theft4::mods::PrepareTimeCycleArchive(game,bundle,cache)==result);
        assert(std::filesystem::last_write_time(result)==modified);
        // Actual VFS reads the private archive, siblings remain original.
        rex::filesystem::VirtualFileSystem fs;
        auto device=std::make_unique<rex::filesystem::HostPathDevice>("\\Device\\Harddisk0\\Partition1",game,true);
        assert(device->Initialize());fs.RegisterDevice(std::move(device));fs.RegisterSymbolicLink("game:","\\Device\\Harddisk0\\Partition1");
        theft4::mods::MountTimeCycle(fs,bundle,result);
        auto* entry=dynamic_cast<rex::filesystem::HostPathEntry*>(fs.ResolvePath("game:/xbox360.rpf"));
        if(entry && entry->host_path()!=result) std::cerr<<"Archive route actual="<<entry->host_path()<<" expected="<<result<<'\n';
        assert(entry && std::filesystem::equivalent(entry->host_path(),result));
        entry=dynamic_cast<rex::filesystem::HostPathEntry*>(fs.ResolvePath("game:/xbox360.rpf.bak"));assert(entry && read(entry->host_path())==Bytes({'b','a','k'}));
        assert(read(game/"xbox360.rpf")==original);
        auto corrupt=read(result);corrupt.back()^=1;write(result,corrupt);
        assert(theft4::mods::PrepareTimeCycleArchive(game,bundle,cache)==result);verify(original,read(result),key,mod);
        auto newer=original;newer.back()=9;write(game/"xbox360.rpf",newer);
        std::filesystem::last_write_time(game/"xbox360.rpf",std::filesystem::last_write_time(game/"xbox360.rpf")+std::chrono::seconds(1));
        auto updated=theft4::mods::PrepareTimeCycleArchive(game,bundle,cache);assert(updated!=result);verify(newer,read(updated),key,mod);
        put(newer,8,999999);write(game/"xbox360.rpf",newer);bool rejected=false;
        try {theft4::mods::PrepareTimeCycleArchive(game,bundle,cache);}catch(const std::runtime_error&){rejected=true;}assert(rejected);
        std::filesystem::remove(game/"xbox360.rpf");assert(theft4::mods::PrepareTimeCycleArchive(game,bundle,cache).empty());
    }
    if(argc==3) {
        const std::filesystem::path real=argv[2];const auto original=read(real/"xbox360.rpf"),realkey=read(real/"aes_key.bin");
        const auto result=theft4::mods::PrepareTimeCycleArchive(real,bundle,root/"real");
        const auto patched=read(result),a=table(original,realkey),b=table(patched,realkey);
        size_t entry=SIZE_MAX;for(size_t i=0;i<a.size();++i)if(a[i]!=b[i]) {const auto e=(i/16)*16;if(entry==SIZE_MAX)entry=e;assert(e==entry);}
        assert(entry!=SIZE_MAX && get(b,entry+4)==mod.size());
        assert(std::equal(mod.begin(),mod.end(),patched.begin()+get(b,entry+8)));
        for(size_t i=0;i<original.size();++i)if(i<0x800 || i>=0x800+a.size())assert(original[i]==patched[i]);
        assert(read(real/"xbox360.rpf")==original);
        std::cout<<"Real encrypted Xbox archive: one entry changed; all original payload bytes preserved.\n";
    }
    std::filesystem::remove_all(root);
    std::cout<<"Packed timecycle, encrypted/plain TOCs, sibling preservation, cache reuse/repair and original-file protection passed.\n";
}
