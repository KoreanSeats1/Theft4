#include "theft4_metal_host_shaders.h"
#include "theft4_host_program.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <set>
namespace theft4::metal {
namespace {bool Fail(std::string& e,const char* m){e=m;return false;}}
bool HostShaderStore::Open(const std::string& directory,std::string& error) {
  try {
    auto path=std::filesystem::path(directory)/"HOST_SHADER_MANIFEST.json";
    if(std::filesystem::file_size(path)>128*1024)return Fail(error,"Host shader manifest exceeds admission budget");
    std::ifstream stream(path);auto json=nlohmann::json::parse(stream);
    if(json.at("schema")!=1||!json.at("rejected").empty()||json.at("programs").empty()||json.at("programs").size()>64)
      return Fail(error,"Host shader manifest is incomplete");
    std::map<std::string,HostShaderMetadata> admitted;
    for(const auto& p:json.at("programs")) {
      const auto name=p.at("name").get<std::string>(),stage=p.at("stage").get<std::string>();
      if(name.empty()||name.size()>80||name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_")!=std::string::npos||
         (stage!="vertex"&&stage!="fragment")||p.at("entry")!="theft4_host_shader")
        return Fail(error,"Invalid host shader identity");
      HostShaderMetadata m;m.resolve_specialization=p.value("resolve_specialization",false);m.stage=stage=="vertex" ? Stage::Vertex : Stage::Fragment;
      if(p.at("constants").size()>1||p.at("textures").size()>16)return Fail(error,"Host shader binding budget exceeded");
      for(const auto& c:p.at("constants")) {
        auto size=c.at("bytes").get<uint64_t>();
        if(c.at("buffer_index")!=0||!size||size>4096)return Fail(error,"Invalid host constant ABI");
        m.constant_bytes=size;
      }
      std::set<uint32_t> bindings,textures,samplers;
      for(const auto& t:p.at("textures")) {
        auto b=t.at("binding").get<uint32_t>(),i=t.at("texture_index").get<uint32_t>(),s=t.at("sampler_index").get<uint32_t>();
        if(t.at("set")!=0||b>31||i>=31||s>=16||t.at("dimension")!=1||t.at("arrayed")!=false||
           !bindings.insert(b).second||!textures.insert(i).second||!samplers.insert(s).second)
          return Fail(error,"Unsupported host texture ABI");
        m.textures.push_back({b,i,s,t.at("multisampled").get<bool>() ? MTLTextureType2DMultisample : MTLTextureType2D});
      }
      if(!admitted.emplace(name,std::move(m)).second)return Fail(error,"Duplicate host shader identity");
    }
    if(admitted.size()!=render::kHostPrograms.size()+1)return Fail(error,"Host program catalog differs from the frame ABI");
    const auto fullscreen=admitted.find("fullscreen_cw_vs");
    if(fullscreen==admitted.end()||fullscreen->second.stage!=Stage::Vertex||fullscreen->second.constant_bytes||
       !fullscreen->second.textures.empty())return Fail(error,"Fullscreen utility vertex ABI differs");
    for(const auto& expected:render::kHostPrograms) {
      const auto found=admitted.find(expected.name);
      if(found==admitted.end()||found->second.stage!=Stage::Fragment||found->second.constant_bytes!=expected.constants)
        return Fail(error,"Host utility constant ABI differs from the frame contract");
      uint32_t textures=0,multisampled=0;
      for(const auto& b:found->second.textures){textures|=1u<<b.binding;if(b.type==MTLTextureType2DMultisample)multisampled|=1u<<b.binding;}
      if(textures!=expected.textures||multisampled!=expected.multisampled)
        return Fail(error,"Host utility input ABI differs from the frame contract");
    }
    catalog_=std::move(admitted);directory_=directory;functions_.clear();libraries_.clear();error.clear();return true;
  }catch(const std::exception& e){error=std::string("Host shader manifest admission: ")+e.what();return false;}
}
const HostShaderMetadata* HostShaderStore::Metadata(const std::string& name) const {
  auto it=catalog_.find(name);return it==catalog_.end() ? nullptr : &it->second;
}
Shader HostShaderStore::Resolve(const std::string& name,std::string& error,std::span<const uint32_t> constants) {
  std::string key=name;for(auto value:constants)key+=":"+std::to_string(value);
  if(auto it=functions_.find(key);it!=functions_.end()){error.clear();return it->second;}
  const auto* m=Metadata(name);if(!m){error="Unknown host utility shader";return {};}
  if(!constants.empty()&&(!m->resolve_specialization||m->stage!=Stage::Fragment||
     m->constant_bytes!=64||constants.size()!=8)) {error="Invalid host resolve specialization ABI";return {};}
  try {
    auto library=libraries_.find(name);
    if(library==libraries_.end()) {
      auto path=std::filesystem::path(directory_)/(name+".metallib");auto size=std::filesystem::file_size(path);
      if(!size||size>16*1024*1024){error="Host Metal library exceeds admission budget";return {};}
      std::vector<uint8_t> bytes(size);std::ifstream stream(path,std::ios::binary);stream.read(reinterpret_cast<char*>(bytes.data()),size);
      if(!stream){error="Could not read the complete host Metal library";return {};}
      auto loaded=renderer_.LoadLibrary(bytes,error);if(!loaded)return {};
      library=libraries_.emplace(name,loaded).first;
    }
    ShaderInterface abi;abi.constant_bytes={m->constant_bytes,0,0};
    for(const auto& b:m->textures){abi.textures|=1u<<b.texture_index;abi.samplers|=1u<<b.sampler_index;abi.texture_types[b.texture_index]=b.type;}
    auto shader=renderer_.LoadShader(library->second,m->stage,abi,0,error,"theft4_host_shader",constants);
    if(shader.function)functions_.emplace(key,shader);return shader;
  }catch(const std::exception& e){error=std::string("Host Metal library: ")+e.what();return {};}
}
bool HostShaderStore::Bind(const std::string& name,std::span<const HostInput> inputs,
                          const BufferView& constants,Draw& draw,std::string& error) const {
  const auto* m=Metadata(name);
  if(!m||inputs.size()!=m->textures.size())return Fail(error,"Incomplete host utility inputs");
  if(m->constant_bytes&&(!constants.buffer||constants.offset%16||constants.offset>constants.buffer.length||
     constants.length>constants.buffer.length-constants.offset||m->constant_bytes>constants.length))
    return Fail(error,"Incomplete host utility constants");
  std::array<const HostInput*,16> admitted{};size_t count=0;
  for(const auto& binding:m->textures) {
    const HostInput* input=nullptr;
    for(const auto& i:inputs)if(i.binding==binding.binding) {
      if(input)return Fail(error,"Duplicate host utility input");input=&i;
    }
    if(!input||!input->texture||input->texture.textureType!=binding.type||!input->sampler)
      return Fail(error,"Invalid host utility texture or sampler type");
    admitted[count++]=input;
  }
  // Replace just this stage after complete admission; a failed binding leaves
  // the previously admitted draw unchanged.
  std::erase_if(draw.textures,[&](const auto& b){return b.stage==m->stage;});
  std::erase_if(draw.samplers,[&](const auto& b){return b.stage==m->stage;});
  draw.textures.reserve(draw.textures.size()+count);draw.samplers.reserve(draw.samplers.size()+count);
  for(size_t i=0;i<count;++i) {
    const auto& binding=m->textures[i];const auto* input=admitted[i];
    draw.textures.push_back({m->stage,binding.texture_index,input->texture});
    draw.samplers.push_back({m->stage,binding.sampler_index,input->sampler});
  }
  if(m->constant_bytes)draw.constants[0]=constants;error.clear();return true;
}
}
