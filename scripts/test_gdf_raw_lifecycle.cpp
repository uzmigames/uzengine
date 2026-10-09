#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>
#define UZ_LOG(...) ((void)0)
struct Vec3 {float x=0,y=0,z=0;};
struct RHITextureHandle {unsigned id=0;bool IsValid()const{return id!=0;}};
enum class RHIFormat {RGBA16Float,R16Float};
enum class RHITextureUsage {ShaderResource=1,UnorderedAccess=2};
RHITextureUsage operator|(RHITextureUsage a,RHITextureUsage b){return RHITextureUsage(unsigned(a)|unsigned(b));}
enum class RHIResourceState {Common,Undefined,ShaderResource,UnorderedAccess};
struct RHITextureDesc {unsigned width=0,height=0,mipLevels=0;uint16_t depthOrArraySize=0;RHIFormat format=RHIFormat::RGBA16Float;RHITextureUsage usage{};bool is3D=false;const char* debugName=nullptr;};
struct Texture {RHITextureDesc desc;RHIResourceState state=RHIResourceState::Common;};
struct RHIBufferHandle {unsigned id=0;bool IsValid()const{return id!=0;}};
enum class RHIBufferUsage {CopyDst};
enum class RHIMemoryType {Readback};
struct RHIBufferDesc {uint64_t sizeBytes=0;unsigned stride=0;RHIBufferUsage usage{};RHIMemoryType memory{};const char* debugName=nullptr;};
struct {float value=0;} fieldCompareCvar;auto* cv_GdfFieldCompare=&fieldCompareCvar;auto* cv_GdfFieldCompareCascade=&fieldCompareCvar;
struct IRHIDevice {
 bool failRawDescriptor=false;unsigned failAt=0,creates=0,destroys=0,errors=0;std::map<unsigned,Texture> textures;
 RHITextureHandle CreateTexture(RHITextureDesc desc){++creates;if(creates==failAt)return{};textures.emplace(creates,Texture{desc});return{creates};}
 void DestroyTexture(RHITextureHandle texture){if(textures.erase(texture.id)!=1)++errors;else ++destroys;}
 RHIBufferHandle CreateBuffer(RHIBufferDesc){return {};}
 void DestroyBuffer(RHIBufferHandle){}
 unsigned GetTextureSRVIndex(RHITextureHandle texture){return failRawDescriptor&&textures.at(texture.id).desc.format==RHIFormat::R16Float?UINT32_MAX:texture.id+100;}
 unsigned GetTextureUAVIndex(RHITextureHandle texture){return texture.id+200;}
 RHITextureDesc GetTextureDesc(RHITextureHandle texture){return textures.at(texture.id).desc;}
};
struct RHIBarrierDesc {RHITextureHandle texture;RHIResourceState stateBefore{},stateAfter{};};
PRODUCTION_CONSTANTS
struct GdfMeshSdfGPU {unsigned char bytes[128];};
constexpr unsigned kGdfMeshesPerBatch=512;
struct CBAllocation {int bufferHandle=1,offsetBytes=0;void* cpuPtr=nullptr;};
struct IRHICommandList {
 IRHIDevice& device;unsigned errors=0,dispatches=0,uavBarriers=0;GdfCompositeConstants current{};
 void SetPipeline(int){}
 void ResourceBarrier(const RHIBarrierDesc* barriers,unsigned count){for(unsigned i=0;i<count;++i){auto& texture=device.textures.at(barriers[i].texture.id);if(texture.state!=barriers[i].stateBefore)++errors;if(barriers[i].stateBefore==RHIResourceState::UnorderedAccess&&barriers[i].stateAfter==RHIResourceState::UnorderedAccess)++uavBarriers;texture.state=barriers[i].stateAfter;}}
 void SetRootConstants(unsigned,const uint32_t* data,unsigned count){if(count!=13){++errors;return;}current=*reinterpret_cast<const GdfCompositeConstants*>(data);}
 void SetRootCBV(int,int,int,void*,unsigned){}
 void Dispatch(unsigned x,unsigned y,unsigned z){++dispatches;auto& expanded=device.textures.at(current.outputUav-200);auto& raw=device.textures.at(current.rawOutputUav-200);if(expanded.desc.format!=RHIFormat::RGBA16Float||raw.desc.format!=RHIFormat::R16Float||expanded.state!=RHIResourceState::UnorderedAccess||raw.state!=RHIResourceState::UnorderedAccess||x!=(current.resolution+3)/4||x!=y||y!=z)++errors;}
};
struct {float value=256;} cvar;auto* cv_GdfResolution=&cvar;
class GlobalDistanceField {
public:
 static constexpr unsigned kNumClipmaps=4;
 static constexpr float kClipmapExtents[4]={2500,10000,40000,160000};
 PRODUCTION_CLIPMAP
 IRHIDevice* m_device=nullptr;ClipmapData m_clipmaps[4];bool m_initialized=false;
 uint64_t m_meshDataHash=0;uint32_t m_meshDataCount=UINT32_MAX;bool m_incremental=true;
 RHIBufferHandle m_fieldBefore,m_fieldAfter;bool m_fieldComparePending=false,m_fieldBeforeCopied=false,m_fieldAwaitSlab=false;uint64_t m_fieldHash=0;
 unsigned m_fieldCompareAge=0,m_fieldCascade=0,m_fieldResolution=0,m_fieldRowPitch=0;Vec3 m_fieldOldCenter,m_fieldNewCenter;float m_fieldExtent=0;
 void CopyRawFieldTo(IRHICommandList*,ClipmapData&,RHIBufferHandle){}
 bool CreateResources(IRHIDevice* device);void DestroyResources();
 void Composite(IRHICommandList* cmdList,unsigned meshCount){int compositePipeline=1;unsigned batchCount=std::max(1u,(meshCount+511)/512);std::vector<CBAllocation>batches(batchCount);uint64_t meshHash=0;PRODUCTION_COMPOSITE}
};
PRODUCTION_CREATE
PRODUCTION_DESTROY
int main(){
 unsigned failures=0,cases=0;auto check=[&](bool pass,const char* name){++cases;if(!pass){++failures;std::printf("FAIL %s\n",name);}};
 for(unsigned fail=0;fail<=8;++fail){
  IRHIDevice device;device.failAt=fail;GlobalDistanceField gdf;bool success=gdf.CreateResources(&device);
  check(success==(fail==0),"allocation outcome");
  if(fail){check(device.textures.empty()&&device.destroys==fail-1&&device.errors==0,"partial allocation cleanup");continue;}
  check(device.textures.size()==8,"four complete raw/expanded pairs");
  for(auto& clip:gdf.m_clipmaps){check(device.textures.at(clip.rawTexture.id).desc.format==RHIFormat::R16Float,"raw R16F format");check(device.textures.at(clip.rawTexture.id).desc.width==256&&device.textures.at(clip.rawTexture.id).desc.is3D,"actual raw volume dimensions");}
  IRHICommandList cmd{device};gdf.Composite(&cmd,513);check(cmd.dispatches==8&&cmd.uavBarriers==8&&cmd.errors==0,"two-batch paired UAV ordering");
  for(auto& clip:gdf.m_clipmaps){check(clip.hasData&&!clip.dirty,"paired publication");clip.dirty=true;}
  gdf.Composite(&cmd,0);check(cmd.dispatches==12&&cmd.errors==0,"recomposite from SRV with empty scene");
  gdf.DestroyResources();check(device.textures.empty()&&device.destroys==8&&device.errors==0,"paired destruction");
 }
 {IRHIDevice device;device.failRawDescriptor=true;GlobalDistanceField gdf;
  check(!gdf.CreateResources(&device)&&device.textures.empty()&&device.destroys==2,"invalid raw descriptor rejects and cleans pair");}
 std::printf("Raw GDF lifecycle: %u/%u passed\n",cases-failures,cases);return failures?1:0;
}
