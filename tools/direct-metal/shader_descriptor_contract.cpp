#include <bit>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <stdexcept>
#include <vector>

// Resolve the shipped RawBufferLoad descriptor ABI before Metal translation.
// Change only descriptor-array access operands, preserving arbitrary constant
// bank arithmetic, texture coordinates, booleans and every shader operation.
std::vector<uint32_t> LowerMetalDescriptorSlots(std::vector<uint32_t> code,
    const std::map<uint32_t,uint32_t>& resources, uint32_t used_mask) {
  if(code.size()<5 || code[0]!=0x07230203 || !code[3] || code[3]>1000000)
    throw std::runtime_error("invalid descriptor module");
  std::vector<std::span<const uint32_t>> definitions(code[3]);
  std::vector<size_t> instructions;
  size_t first_function=SIZE_MAX;
  for(size_t p=5;p<code.size();) {
    const size_t n=code[p]>>16; const uint32_t op=code[p]&65535;
    if(!n || n>code.size()-p)throw std::runtime_error("invalid descriptor instruction");
    auto w=std::span<const uint32_t>(code).subspan(p,n);instructions.push_back(p);
    size_t index=0;
    switch(op){case 21:case 22:case 23:case 28:case 29:case 30:case 32:index=1;break;
      case 43:case 59:case 61:case 65:case 66:case 83:case 113:case 114:
      case 120:case 124:case 128:case 169:case 245:index=2;break;}
    if(index){if(n<=index || !w[index] || w[index]>=definitions.size())
      throw std::runtime_error("invalid descriptor result");definitions[w[index]]=w;}
    if(op==54 && first_function==SIZE_MAX)first_function=p;
    p+=n;
  }
  if(first_function==SIZE_MAX)throw std::runtime_error("missing shader functions");
  auto def=[&](uint32_t id){return id<definitions.size()?definitions[id]:std::span<const uint32_t>{};};
  std::function<bool(uint32_t,uint64_t&,unsigned)> integer=[&](uint32_t id,uint64_t& value,unsigned depth){
    if(depth>32)return false;auto w=def(id);if(w.size()<4)return false;
    const auto op=w[0]&65535;
    if((op==83||op==113||op==114||op==124)&&w.size()==4)return integer(w[3],value,depth+1);
    if(op!=43)return false;auto t=def(w[1]);
    if(t.size()!=4||(t[0]&65535)!=21||(t[2]!=32&&t[2]!=64)||w.size()!=(t[2]==64?5:4))return false;
    value=w[3];if(t[2]==64)value|=uint64_t(w[4])<<32;return true;
  };
  struct Address {int bank=-1;uint64_t offset=0;};
  std::function<Address(uint32_t,unsigned)> address=[&](uint32_t id,unsigned depth)->Address {
    if(depth>32)return {};auto w=def(id);if(w.size()<4)return {};
    const uint32_t op=w[0]&65535;
    if((op==83||op==113||op==114||op==120||op==124)&&w.size()==4)return address(w[3],depth+1);
    if(op==128&&w.size()==5){
      uint64_t offset;uint32_t base;
      if(integer(w[4],offset,0))base=w[3];else if(integer(w[3],offset,0))base=w[4];else return {};
      auto a=address(base,depth+1);if(a.bank<0||offset>UINT64_MAX-a.offset)return {};
      a.offset+=offset;return a;
    }
    if(op!=61)return {};
    auto access=def(w[3]);if(access.size()!=5||((access[0]&65535)!=65&&(access[0]&65535)!=66))return {};
    auto variable=def(access[3]);if(variable.size()!=4||(variable[0]&65535)!=59||variable[3]!=9)return {};
    auto pointer=def(variable[1]);if(pointer.size()!=4||(pointer[0]&65535)!=32||pointer[2]!=9)return {};
    auto structure=def(pointer[3]);if(structure.size()!=5||(structure[0]&65535)!=30)return {};
    for(size_t member=2;member<5;++member){auto type=def(structure[member]);
      if(type.size()!=4||(type[0]&65535)!=21||type[2]!=64)return {};}
    uint64_t member;if(!integer(access[4],member,0)||member>2)return {};
    return {int(member),0};
  };
  std::function<Address(uint32_t,unsigned)> descriptor=[&](uint32_t id,unsigned depth)->Address {
    if(depth>32)return {};auto w=def(id);if(w.size()<4)return {};
    const auto op=w[0]&65535;
    if((op==83||op==113||op==114||op==124)&&w.size()==4)return descriptor(w[3],depth+1);
    if(op!=61)return {};
    return address(w[3],0);
  };
  uint32_t next=code[3];std::vector<uint32_t> constants;
  std::map<std::pair<uint32_t,uint32_t>,uint32_t> values;
  for(size_t p:instructions){
    const uint32_t op=code[p]&65535,n=code[p]>>16;
    if(op!=65&&op!=66)continue;
    auto resource=resources.find(code[p+3]);if(resource==resources.end())continue;
    if(n!=5)throw std::runtime_error("multidimensional descriptor access");
    const uint32_t index=code[p+4];auto a=descriptor(index,0);
    if(a.bank!=2||a.offset>=520||a.offset%4||a.offset/104!=resource->second)
      throw std::runtime_error("unresolved or mismatched descriptor fetch address");
    const uint32_t slot=uint32_t(a.offset%104)/4;
    if(!(used_mask&(1u<<slot)))throw std::runtime_error("unreflected descriptor fetch slot");
    const uint32_t rank=std::popcount(used_mask&((1u<<slot)-1));
    auto index_def=def(index);if(index_def.size()<3)throw std::runtime_error("missing descriptor index type");
    const uint32_t type=index_def[1];auto type_def=def(type);
    if(type_def.size()!=4||(type_def[0]&65535)!=21||(type_def[2]!=32&&type_def[2]!=64))
      throw std::runtime_error("non-integer descriptor index");
    auto [value,inserted]=values.try_emplace({type,rank},next);
    if(inserted){++next;constants.insert(constants.end(),{uint32_t((type_def[2]==64?5:4)<<16)|43u,type,value->second,rank});
      if(type_def[2]==64)constants.push_back(0);}
    code[p+4]=value->second;
  }
  code[3]=next;code.insert(code.begin()+first_function,constants.begin(),constants.end());
  return code;
}
