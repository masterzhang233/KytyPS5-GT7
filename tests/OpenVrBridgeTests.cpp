#include "graphics/vr/openvrBridge.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <future>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace Libs::Graphics::Vr;
static int tests=0;
#define CHECK(x) do { if(!(x)) throw std::runtime_error(std::string(#x)+" at "+std::to_string(__LINE__)); } while(false)
static void Pass(const char* name) { ++tests; std::printf("PASS %s\n",name); }
template<class T> T Handle(uintptr_t n) { return reinterpret_cast<T>(n); }
static Pose Identity() { Pose p; p.connected=p.valid=true; p.tracking_result=200; for(int i=0;i<3;++i)p.device_to_absolute[i][i]=1; return p; }
static EyeTexture Eye(uintptr_t n) { EyeTexture t; t.image=Handle<VkImage>(n); t.width=1024; t.height=1024; t.format=VK_FORMAT_R8G8B8A8_UNORM; t.layout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; t.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT; return t; }
static VulkanBinding Binding() { return {Handle<VkInstance>(1),Handle<VkPhysicalDevice>(2),Handle<VkDevice>(3),Handle<VkQueue>(4),0}; }
struct Fake: RuntimeBackend {
    bool init_ok=true, connected=true, pose_ok=true;
    int init_count=0,shutdown_count=0,wait_count=0,submit_count=0,handoff_count=0,clear_count=0,fail_eye=-1;
    int tracking_count=0;
    VkPhysicalDevice required=Handle<VkPhysicalDevice>(2);
    std::atomic<int> concurrent {0}, max_concurrent {0};
    Status Initialize(const std::filesystem::path&,TrackingOrigin) override { ++init_count; return init_ok?Status::Success():Status::Failure("init"); }
    void Shutdown() noexcept override { ++shutdown_count; }
    Status ReadHeadset(HeadsetInfo& p) override { p.connected=connected; return connected?Status::Success():Status::Failure("disconnected"); }
    Status ReadDisplayTiming(DisplayTiming& p) override { p={90,.002f,.003f};return Status::Success(); }
    Status ReadPose(float seconds,Pose& out) override {
        const int n=++concurrent; max_concurrent.store(std::max(max_concurrent.load(),n));
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        out=Identity(); out.prediction_seconds=seconds; out.connected=connected; out.valid=pose_ok;
        --concurrent; return connected?Status::Success():Status::Failure("disconnected");
    }
    Status ReadTrackingSample(float seconds,Pose& out,std::array<EyeGeometry,2>& eyes) override {
        ++tracking_count;eyes={};eyes[0].eye_to_head[0][3]=-.031f;eyes[1].eye_to_head[0][3]=.034f;
        return ReadPose(seconds,out);
    }
    Status WaitPose(Pose& p) override { ++wait_count; p=Identity(); p.valid=pose_ok; return Status::Success(); }
    Status InstanceExtensions(std::vector<std::string>& v) override { v={"VK_KHR_surface"}; return Status::Success(); }
    Status OutputDevice(VkInstance,VkPhysicalDevice& d) override { d=required; return Status::Success(); }
    Status DeviceExtensions(VkPhysicalDevice,std::vector<std::string>& v) override { v={"VK_KHR_external_memory"}; return Status::Success(); }
    Status SubmitEye(uint32_t eye,const VulkanBinding&,const EyeTexture&,const Pose*) override { ++submit_count; return static_cast<int>(eye)==fail_eye?Status::Failure("submit",100):Status::Success(); }
    void Handoff() override { ++handoff_count; }
    void ClearFrame() override { ++clear_count; }
};
static bool Near(float a,float b) { return std::abs(a-b)<1e-4f; }
struct BlockGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered=false, released=false;
    void Block() {
        std::unique_lock lock(mutex); entered=true;changed.notify_all();
        changed.wait(lock,[&]{return released;});
    }
    bool Entered() {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock,std::chrono::seconds(2),[&]{return entered;});
    }
    void Release() { std::scoped_lock lock(mutex);released=true;changed.notify_all(); }
};
struct BlockingFake: Fake {
    int blocked_operation=0; // 0: compositor wait, 1: left submit, 2: live tracking
    BlockGate gate;
    std::atomic<bool> external_in_flight{false}, shutdown_while_in_flight{false};
    void Block(int operation) {
        if(blocked_operation!=operation)return;
        external_in_flight=true;gate.Block();external_in_flight=false;
    }
    Status WaitPose(Pose& p) override {Block(0);return Fake::WaitPose(p);}
    Status SubmitEye(uint32_t eye,const VulkanBinding& b,const EyeTexture& t,const Pose* p) override {
        if(eye==0)Block(1);return Fake::SubmitEye(eye,b,t,p);
    }
    Status ReadPose(float seconds,Pose& p) override {Block(2);return Fake::ReadPose(seconds,p);}
    void Shutdown() noexcept override {
        shutdown_while_in_flight=external_in_flight.load();Fake::Shutdown();
    }
};
static void TestTrackingConcurrency() {
    for(int operation: {0,1}) {
        auto f=std::make_unique<BlockingFake>();auto* p=f.get();p->blocked_operation=operation;
        OpenVrBridge b(std::move(f));CHECK(b.Initialize(L"ignored"));CHECK(b.AttachVulkan(Binding()));
        FramePose frame;
        if(operation==1)CHECK(b.WaitFrame(frame));
        auto compositor=std::async(std::launch::async,[&]{
            return operation==0 ? bool(b.WaitFrame(frame)) :
                bool(b.SubmitStereo(frame,Eye(10),Eye(11),StereoSource::GuestSeparateEyes).status);
        });
        const bool entered=p->gate.Entered();
        auto tracking=std::async(std::launch::async,[&]{
            Pose pose;HeadsetInfo info;DisplayTiming timing;std::array<EyeGeometry,2> eyes;
            return b.Active()&&b.ReadHeadset(info)&&b.ReadDisplayTiming(timing)&&
                b.ReadPredictedPose(.01f,pose)&&b.ReadTrackingSample(.02f,pose,eyes)&&
                pose.valid&&Near(pose.prediction_seconds,.02f)&&Near(eyes[0].eye_to_head[0][3],-.031f);
        });
        const bool independent=tracking.wait_for(std::chrono::milliseconds(200))==std::future_status::ready;
        p->gate.Release();const bool rendered=compositor.get(),tracked=tracking.get();
        CHECK(entered&&independent&&rendered&&tracked);
        Pass(operation==0?"tracking_proceeds_during_blocked_compositor_wait":"tracking_proceeds_during_blocked_eye_submit");
    }
    for(int operation: {0,2}) {
        auto f=std::make_unique<BlockingFake>();auto* p=f.get();p->blocked_operation=operation;
        OpenVrBridge b(std::move(f));CHECK(b.Initialize(L"ignored"));CHECK(b.AttachVulkan(Binding()));
        auto io=std::async(std::launch::async,[&]{FramePose frame;Pose pose;
            return operation==0?bool(b.WaitFrame(frame)):bool(b.ReadPredictedPose(.01f,pose));});
        const bool entered=p->gate.Entered();
        std::promise<void> shutdown_started;
        auto shutdown=std::async(std::launch::async,[&]{shutdown_started.set_value();b.Shutdown();});
        shutdown_started.get_future().wait();
        const bool held=shutdown.wait_for(std::chrono::milliseconds(50))==std::future_status::timeout;
        p->gate.Release();const bool success=io.get();shutdown.get();
        CHECK(entered&&held&&success&&!p->shutdown_while_in_flight&&p->shutdown_count==1&&!b.Active());
        CHECK(b.Initialize(L"ignored"));CHECK(b.AttachVulkan(Binding()));
        Pass(operation==0?"shutdown_waits_for_compositor_before_reconnect":"shutdown_waits_for_tracking_before_reconnect");
    }
}
static int DiagnosticFixture() {
    auto f=std::make_unique<Fake>();auto* runtime=f.get();OpenVrBridge bridge(std::move(f));
    CHECK(bridge.Initialize(L"ignored"));CHECK(bridge.AttachVulkan(Binding()));
    const float p[3]={1,2,3},q[4]={0,std::sqrt(.5f),0,std::sqrt(.5f)};Pose render;
    CHECK(MakeRenderPose(p,q,render));auto left=Eye(10),right=Eye(11);right.bounds={.1f,.2f,.9f,.8f};right.gamma=false;
    for(unsigned i=0;i<40;++i){FramePose frame;CHECK(bridge.WaitFrame(frame));
        if(i==0)CHECK(!bridge.SubmitStereo(frame,left,right,StereoSource::Unverified,&render).status);
        runtime->fail_eye=i==8?1:-1;
        auto submitted=bridge.SubmitStereo(frame,left,right,StereoSource::GuestSeparateEyes,i==16?nullptr:&render);
        CHECK(submitted.left_accepted&&submitted.right_accepted==(i!=8));CHECK(submitted.queue_may_have_work);
        CHECK(bridge.PostPresentHandoff());
    }
    CHECK(runtime->submit_count==80&&runtime->handoff_count==40&&runtime->clear_count==1);
    std::puts("DIAGNOSTIC_BACKEND submits=80 handoffs=40 clears=1 frames=40");return 0;
}
int main(int argc,char** argv) { try {
    if(argc==2&&std::strcmp(argv[1],"--diagnostics-fixture")==0)return DiagnosticFixture();
    TestTrackingConcurrency();
    { auto p=Identity(); p.device_to_absolute[0][3]=1.25f; p.device_to_absolute[1][3]=-2; CHECK(NormalizePose(p)); CHECK(Near(p.orientation_xyzw[3],1)); CHECK(Near(p.position_m[0],1.25f)); CHECK(Near(p.position_m[1],-2)); Pass("identity_and_metres"); }
    { for(int axis=0;axis<3;++axis) { auto p=Identity(); for(int k=0;k<3;++k)p.device_to_absolute[k][k]=k==axis?1.0f:-1.0f; CHECK(NormalizePose(p)); CHECK(Near(std::abs(p.orientation_xyzw[axis]),1)); CHECK(Near(p.orientation_xyzw[3],0)); } Pass("all_quaternion_180_branches"); }
    { auto p=Identity(); p.device_to_absolute[0][0]=0; p.device_to_absolute[0][2]=1; p.device_to_absolute[2][0]=-1;p.device_to_absolute[2][2]=0; CHECK(NormalizePose(p)); CHECK(Near(p.orientation_xyzw[1],std::sqrt(.5f))); CHECK(Near(p.orientation_xyzw[3],std::sqrt(.5f))); Pass("yaw_sign_and_xyzw_order"); }
    { const float pos[3]={1,2,3};float q[4]={0,std::sqrt(.5f),0,std::sqrt(.5f)};Pose p;CHECK(MakeRenderPose(pos,q,p));CHECK(p.source==PoseSource::GuestRendered&&!p.connected&&p.tracking_result==0&&p.sample_monotonic_ns==0);CHECK(Near(p.device_to_absolute[0][2],1)&&Near(p.device_to_absolute[2][0],-1)&&Near(p.position_m[2],3));q[1]=0;q[3]=0;CHECK(!MakeRenderPose(pos,q,p));CHECK(!p.valid);q[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!MakeRenderPose(pos,q,p));Pass("guest_render_pose_math_without_fake_tracking"); }
    { Pose a=Identity(),b=Identity();float angle=99,distance=99;CHECK(CalculatePoseDelta(a,b,angle,distance));CHECK(Near(angle,0)&&Near(distance,0));
      const float pos[3]={1,2,3},q[4]={0,std::sqrt(.5f),0,std::sqrt(.5f)};CHECK(MakeRenderPose(pos,q,b));CHECK(CalculatePoseDelta(a,b,angle,distance));CHECK(Near(angle,90)&&Near(distance,std::sqrt(14.0f)));
      float negq[4]={0,-q[1],0,-q[3]};Pose same;CHECK(MakeRenderPose(pos,negq,same));CHECK(CalculatePoseDelta(b,same,angle,distance));CHECK(angle<.05f&&Near(distance,0));
      b=Identity();b.device_to_absolute[0][0]=-1;b.device_to_absolute[1][1]=-1;CHECK(CalculatePoseDelta(a,b,angle,distance));CHECK(Near(angle,180));b.device_to_absolute[0][0]=NAN;CHECK(!CalculatePoseDelta(a,b,angle,distance));CHECK(angle==0&&distance==0);Pass("diagnostic_relative_rigid_pose_math_no_mutation"); }
    { for(int kind=0;kind<6;++kind){ auto p=Identity(); if(kind==0)p.connected=false; if(kind==1)p.valid=false; if(kind==2)p.tracking_result=300; if(kind==3)p.device_to_absolute[0][0]=-1; if(kind==4)p.device_to_absolute[0][0]=2; if(kind==5)p.linear_velocity_m_s[1]=std::numeric_limits<float>::quiet_NaN(); CHECK(!NormalizePose(p)); } Pass("reject_disconnected_rotation_only_nan_reflection_scale"); }
    { std::vector<std::string> v; const char s[]="VK_KHR_surface  VK_EXT_debug_utils VK_KHR_surface"; CHECK(ParseExtensionList(s,sizeof(s),v)); CHECK(v.size()==2); CHECK(ParseExtensionList("",1,v)); CHECK(v.empty()); CHECK(!ParseExtensionList("VK_A",4,v)); CHECK(!ParseExtensionList("VK_A\0VK_B",10,v)); CHECK(!ParseExtensionList("VK_A;bad",9,v)); CHECK(!ParseExtensionList("X_bad",6,v)); Pass("extension_termination_bounds_dedup"); }
    { auto l=Eye(10),r=Eye(11); CHECK(ValidateStereo(l,r,StereoSource::GuestSeparateEyes)); CHECK(!ValidateStereo(l,l,StereoSource::GuestSeparateEyes)); CHECK(!ValidateStereo(l,r,StereoSource::Unverified)); CHECK(!ValidateStereo(l,r,static_cast<StereoSource>(99))); Pass("reject_flat_duplication_unknown_provenance"); }
    { auto l=Eye(10),r=Eye(10); l.bounds.u_max=.5f;r.bounds.u_min=.5f; CHECK(ValidateStereo(l,r,StereoSource::GuestSideBySide)); r.bounds.u_min=.4f; CHECK(!ValidateStereo(l,r,StereoSource::GuestSideBySide)); r.bounds.u_min=.5f;r.width=512; CHECK(!ValidateStereo(l,r,StereoSource::GuestSideBySide)); Pass("sbs_disjoint_geometry"); }
    { auto l=Eye(10),r=Eye(10); l.array_layers=r.array_layers=2;r.array_index=1; CHECK(ValidateStereo(l,r,StereoSource::GuestArrayLayers)); r.array_index=0;CHECK(!ValidateStereo(l,r,StereoSource::GuestArrayLayers));r.array_index=2;CHECK(!ValidateStereo(l,r,StereoSource::GuestArrayLayers));Pass("real_array_layers_distinct_and_in_range"); }
    { for(int kind=0;kind<9;++kind){ auto l=Eye(10),r=Eye(11);if(kind==0)l.layout=VK_IMAGE_LAYOUT_GENERAL;if(kind==1)l.width=0;if(kind==2)l.samples=4;if(kind==3)l.mip_levels=2;if(kind==4)l.format=VK_FORMAT_D32_SFLOAT;if(kind==5)l.bounds.u_max=2;if(kind==6)l.bounds.v_min=std::numeric_limits<float>::infinity();if(kind==7)l.array_layers=0;if(kind==8)l.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT; CHECK(!ValidateStereo(l,r,StereoSource::GuestSeparateEyes)); }Pass("texture_layout_format_extent_subresource_usage_checks"); }
    { DisplayTiming t{90,.002f,.003f};CHECK(CalculateDisplayTiming(t));CHECK(Near(t.prediction_seconds,1.0f/90+.001f));
      t={90,-.018738f,.011111f};CHECK(CalculateDisplayTiming(t));CHECK(Near(t.prediction_seconds,.04096011f));CHECK(t.seconds_since_last_vsync<0);
      for(int kind=0;kind<6;++kind){ t={90,.002f,.003f};if(kind==0)t.frequency_hz=0;if(kind==1)t.frequency_hz=std::numeric_limits<float>::quiet_NaN();if(kind==2)t.seconds_since_last_vsync=1;if(kind==3)t.seconds_from_vsync_to_photons=-1;if(kind==4)t.frequency_hz=1;if(kind==5)t.seconds_since_last_vsync=-1;CHECK(!CalculateDisplayTiming(t)); }Pass("actual_signed_virtual_vsync_and_stale_timing"); }
    { auto f=std::make_unique<Fake>(); auto* p=f.get(); p->init_ok=false;OpenVrBridge b(std::move(f)); CHECK(!b.Initialize(L"ignored"));CHECK(!b.Active());CHECK(p->shutdown_count==1);Pose pose=Identity();CHECK(!b.ReadPredictedPose(0,pose));CHECK(!pose.valid);Pass("init_failure_and_no_stale_output"); }
    { auto f=std::make_unique<Fake>();auto* p=f.get();OpenVrBridge b(std::move(f));CHECK(b.Initialize(L"ignored"));CHECK(!b.Initialize(L"ignored"));auto binding=Binding();binding.physical_device=Handle<VkPhysicalDevice>(5);CHECK(!b.AttachVulkan(binding));CHECK(b.AttachVulkan(Binding()));CHECK(!b.AttachVulkan(Binding()));std::vector<std::string> v;CHECK(b.RequiredInstanceExtensions(v));CHECK(v.size()==1);CHECK(b.RequiredDeviceExtensions(p->required,v));Pass("runtime_gpu_match_and_extension_queries"); }
    { auto f=std::make_unique<Fake>();auto* p=f.get();OpenVrBridge b(std::move(f));CHECK(b.Initialize(L"ignored"));Pose pose;CHECK(b.ReadPredictedPose(.01f,pose));CHECK(pose.valid);for(float t:{-.1f,.26f,std::numeric_limits<float>::quiet_NaN()})CHECK(!b.ReadPredictedPose(t,pose));p->connected=false;pose=Identity();CHECK(!b.ReadPredictedPose(0,pose));CHECK(!pose.valid&&!pose.connected);p->connected=true;p->pose_ok=false;CHECK(!b.ReadPredictedPose(0,pose));Pass("prediction_and_disconnect_invalidate_pose"); }
    { auto f=std::make_unique<Fake>();auto* p=f.get();OpenVrBridge b(std::move(f));CHECK(b.Initialize(L"ignored"));FramePose frame;CHECK(!b.WaitFrame(frame));CHECK(b.AttachVulkan(Binding()));CHECK(b.WaitFrame(frame));auto r=b.SubmitStereo(frame,Eye(10),Eye(11),StereoSource::GuestSeparateEyes);CHECK(r.status&&r.left_accepted&&r.right_accepted&&r.queue_may_have_work);CHECK(!b.SubmitStereo(frame,Eye(10),Eye(11),StereoSource::GuestSeparateEyes).status);CHECK(p->submit_count==2);CHECK(b.PostPresentHandoff());CHECK(b.PostPresentHandoff());CHECK(p->handoff_count==1);Pass("frame_lifecycle_exactly_two_submits"); }
    { auto f=std::make_unique<Fake>();auto* p=f.get();OpenVrBridge b(std::move(f));CHECK(b.Initialize(L"ignored"));CHECK(b.AttachVulkan(Binding()));FramePose frame;CHECK(b.WaitFrame(frame));p->fail_eye=1;auto r=b.SubmitStereo(frame,Eye(10),Eye(11),StereoSource::GuestSeparateEyes);CHECK(!r.status&&r.left_accepted&&!r.right_accepted&&r.queue_may_have_work);CHECK(p->clear_count==1);CHECK(!b.SubmitStereo(frame,Eye(10),Eye(11),StereoSource::GuestSeparateEyes).status);CHECK(b.WaitFrame(frame));CHECK(p->handoff_count==1);Pass("partial_submit_requires_retirement_and_no_retry"); }
    { auto f=std::make_unique<Fake>();OpenVrBridge b(std::move(f));CHECK(b.Initialize(L"ignored"));CHECK(b.AttachVulkan(Binding()));FramePose old,now;CHECK(b.WaitFrame(old));b.Shutdown();CHECK(b.Initialize(L"ignored"));CHECK(b.AttachVulkan(Binding()));CHECK(b.WaitFrame(now));CHECK(now.token>old.token);CHECK(!b.SubmitStereo(old,Eye(10),Eye(11),StereoSource::GuestSeparateEyes).status);Pose invalid=Identity();invalid.valid=false;CHECK(!b.SubmitStereo(now,Eye(10),Eye(11),StereoSource::GuestSeparateEyes,&invalid).status);CHECK(b.SubmitStereo(now,Eye(10),Eye(11),StereoSource::GuestSeparateEyes).status);Pass("reconnect_token_and_render_pose_validation"); }
    { auto f=std::make_unique<Fake>();auto* p=f.get();OpenVrBridge b(std::move(f));CHECK(b.Initialize(L"ignored"));std::vector<std::thread> threads;std::atomic<int> failures{0};for(int i=0;i<8;++i)threads.emplace_back([&]{for(int n=0;n<50;++n){Pose pose;if(!b.ReadPredictedPose(0,pose)||!pose.valid)++failures;}});for(auto& t:threads)t.join();CHECK(failures==0&&p->max_concurrent==1);Pass("eight_thread_pose_serialization"); }
    { OpenVrBridge actual;CHECK(!actual.Initialize(L"relative/openvr_api.dll"));CHECK(!actual.Active());Pass("real_backend_rejects_relative_DLL_without_loading"); }
    { auto f=std::make_unique<Fake>();auto* p=f.get();OpenVrBridge b(std::move(f));CHECK(b.Initialize(L"ignored"));Pose pose;std::array<EyeGeometry,2> eyes;
      CHECK(b.ReadTrackingSample(.02f,pose,eyes));CHECK(p->tracking_count==1&&pose.valid&&Near(pose.prediction_seconds,.02f));
      CHECK(Near(eyes[0].eye_to_head[0][3],-.031f)&&Near(eyes[1].eye_to_head[0][3],.034f));
      p->connected=false;CHECK(!b.ReadTrackingSample(0,pose,eyes));CHECK(!pose.valid&&!pose.connected&&eyes[0].eye_to_head[0][3]==0);
      p->connected=true;p->pose_ok=false;CHECK(!b.ReadTrackingSample(0,pose,eyes));CHECK(!pose.valid&&eyes[1].eye_to_head[0][3]==0);
      CHECK(!b.ReadTrackingSample(.26f,pose,eyes));CHECK(p->tracking_count==3);Pass("combined_tracking_sample_preserves_eyes_and_invalidates_failures"); }
    std::printf("PASSED %d groups (runtime I/O mocked; no headset/GPU launched)\n",tests);return 0;
} catch(const std::exception& e) { std::fprintf(stderr,"FAILED: %s\n",e.what());return 1; } }
