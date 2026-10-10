#include <cstdint>
#include <cstdio>
#include <memory>
#include <string_view>
struct PSO { bool valid; bool IsValid() const { return valid; } };
struct PipelineLibrary {
 bool valid=true;
 PSO GetSsgiTrace(){return {valid};} PSO GetSsgiSpatialDenoise(){return {valid};}
 PSO GetSsgiTemporalAccum(){return {valid};} PSO GetSsgiBilateralUpsample(){return {valid};}
 PSO GetSsgiPrevColorReduce(){return {valid};}
};
struct Registry {
 bool screen=false,world=false,radianceCache=false;
 bool GetBool(std::string_view name,bool) const {
  if(name=="r.SSGI")return screen;
  if(name=="r.GI.FinalGather")return world;
  if(name=="r.RadianceCache")return radianceCache;
  return false;
 }
};
struct CVar {float value=0;}; CVar cvar; CVar* cv_SSGIEnabled=&cvar;
struct SSGIRenderer {
 bool m_worldGather=false,m_hasHistory=false,m_hasProducedResult=false,m_initialized=true,m_hasPrevColor=false;
 struct {bool enabled=false;} settings;
 bool PrepareFrame(PipelineLibrary* pipelines,bool worldGather,bool screenSpace);
 void LogGatherActivation(uint32_t){};
 bool IsReady() const{return m_initialized&&settings.enabled;}
 uint32_t GetResultSrvIndex()const{return 41;} uint32_t GetAOResultSrvIndex()const{return 42;}
};
PRODUCTION_PREPARE
struct PerFrame {uint32_t sdfDiffuseSamples=0,ssgiSrvIndex=UINT32_MAX,ssgiAOSrvIndex=UINT32_MAX,ssgiEnabled=0;};
struct SwapChain {int GetWidth(){return 960;}int GetHeight(){return 540;}};
struct FrameContext {PerFrame perFrame;PipelineLibrary* pipelines=nullptr;SwapChain* swapChain;float vpWidth=0,vpHeight=0,renderWidth=0,renderHeight=0;};
enum class ShowFlag {SSGI};
struct Flags {bool Get(ShowFlag){return false;}};
struct Inspector {
 bool ssgi=false,sdfGi=false;
 void SetPassEnabled(std::string_view name,bool value){if(name=="ssgi")ssgi=value;else if(name=="sdf_gi")sdfGi=value;}
 bool IsPassEnabled(std::string_view name){return name=="ssgi"?ssgi:sdfGi;}
};
struct Device {bool bindless=true;bool IsBindless(){return bindless;}};
struct SceneRenderer {
 std::unique_ptr<PipelineLibrary> m_pipelines=std::make_unique<PipelineLibrary>();
 SSGIRenderer m_ssgiRenderer;Flags m_showFlags;Inspector m_passInspector;Registry cvars;
 Device device;Device* m_device=&device;int m_renderWidth=960,m_renderHeight=540;
 void PopulatePerFrameConstants(FrameContext& ctx){auto& perFrame=ctx.perFrame;PRODUCTION_BINDING}
 void SyncDrawListAndInstances(FrameContext&){}
 void ApplyCVars(){PRODUCTION_APPLY_CVARS}
 void Setup(FrameContext& ctx){PRODUCTION_SETUP}
 bool DiagnosticTargetsActive(FrameContext& ctx){PRODUCTION_DIAGNOSTIC return ssgiActive;}
};
int main(){
 unsigned cases=0,failures=0;
 for(bool fg:{false,true})for(unsigned available:{0u,8u})for(bool screen:{false,true})
 for(bool ready:{false,true})for(bool radiance:{false,true})for(bool bindless:{false,true}){
  SceneRenderer renderer;renderer.m_pipelines->valid=ready;renderer.device.bindless=bindless;
  renderer.cvars={screen,fg,radiance};cv_SSGIEnabled->value=screen?1.f:0.f;
  for(unsigned frame=0;frame<3;++frame){
   SwapChain swap;FrameContext ctx;ctx.swapChain=&swap;ctx.perFrame.sdfDiffuseSamples=fg?available:0u;
   renderer.Setup(ctx);
   bool expected=fg&&available&&ready&&radiance&&bindless;
   bool actual=ctx.perFrame.sdfDiffuseSamples!=0 && ctx.perFrame.ssgiSrvIndex==41 && ctx.perFrame.ssgiEnabled==1;
   ++cases;
   if(actual!=expected || (expected&&!renderer.DiagnosticTargetsActive(ctx)) || ctx.pipelines!=renderer.m_pipelines.get()){
    ++failures;std::printf("FAIL frame=%u FG=%d available=%u SSGI=%d ready=%d RC=%d bindless=%d samples=%u srv=%u\n",frame,fg,available,screen,ready,radiance,bindless,ctx.perFrame.sdfDiffuseSamples,ctx.perFrame.ssgiSrvIndex);
   }
  }
 } std::printf("Gather activation lifecycle: %u/%u passed\n",cases-failures,cases);
 return failures?1:0;
}
