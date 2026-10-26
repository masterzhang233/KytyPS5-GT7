#include "graphics/vr/openvrBridge.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

// Real scene runtime connection only: no Vulkan, eye submission, GT7 launch or UI automation.
int wmain(int argc,wchar_t** argv) {
    using namespace Libs::Graphics::Vr;
    std::filesystem::path dll,output;
    for(int i=1;i<argc;++i) {
        std::wstring arg=argv[i];
        if(arg==L"--dll"&&i+1<argc)dll=argv[++i];
        else if(arg==L"--json"&&i+1<argc)output=argv[++i];
        else { std::cerr<<"Usage: VrRuntimeProbe --dll <absolute openvr_api.dll> [--json <new report.json>]\n";return 2; }
    }
    if(dll.empty()) { std::cerr<<"An explicit installed SteamVR DLL path is required\n";return 2; }
    if(!output.empty()&&std::filesystem::exists(output)){std::cerr<<"Refusing to overwrite existing probe report\n";return 2;}
    nlohmann::json report={{"schema",1},{"kind","real_openvr_headset_pose_probe"},{"game_started",false},{"images_submitted",false}};
    int exit_code=1;
    try {
        OpenVrBridge bridge;
        auto status=bridge.Initialize(dll);
        report["initialized"]=status.ok;
        if(status) {
            HeadsetInfo info;status=bridge.ReadHeadset(info);
            report["headset_read"]=status.ok;
            if(status) {
                report["headset"]={{"connected",info.connected},{"model",info.model},
                    {"recommended_eye_width",info.recommended_eye_width},{"recommended_eye_height",info.recommended_eye_height},
                    {"display_frequency_known",info.display_frequency_known},{"display_frequency_hz",info.display_frequency_hz},
                    {"ipd_known",info.ipd_known},{"ipd_m",info.ipd_m},{"activity_level",info.activity_level}};
                report["headset"]["eyes"]=nlohmann::json::array();
                for(const auto& eye:info.eyes)report["headset"]["eyes"].push_back({{"eye_to_head",eye.eye_to_head},
                    {"projection_raw",{eye.left,eye.right,eye.top,eye.bottom}}});
                DisplayTiming timing; auto timing_status=bridge.ReadDisplayTiming(timing);
                report["display_timing_read"]=timing_status.ok;
                report["display_timing_error"]=timing_status.message;
                if(timing_status)report["display_timing"]={{"frequency_hz",timing.frequency_hz},
                    {"seconds_since_last_vsync",timing.seconds_since_last_vsync},
                    {"seconds_from_vsync_to_photons",timing.seconds_from_vsync_to_photons},
                    {"prediction_seconds",timing.prediction_seconds},{"vsync_counter",timing.vsync_counter},
                    {"sample_monotonic_ns",timing.sample_monotonic_ns}};
                Pose pose;status=bridge.ReadPredictedPose(0,pose);
                report["pose_read"]=status.ok;
                if(status) {
                    report["pose"]={{"connected",pose.connected},{"valid",pose.valid},{"tracking_result",pose.tracking_result},
                        {"position_m",pose.position_m},{"orientation_xyzw",pose.orientation_xyzw},
                        {"device_to_absolute",pose.device_to_absolute},{"linear_velocity_m_s",pose.linear_velocity_m_s},
                        {"angular_velocity_rad_s",pose.angular_velocity_rad_s},{"sample_monotonic_ns",pose.sample_monotonic_ns},
                        {"prediction_seconds",pose.prediction_seconds},{"coordinate_system","OpenVR right handed +Y up -Z forward, metres"}};
                    std::vector<std::string> extensions;status=bridge.RequiredInstanceExtensions(extensions);
                    if(status)report["required_instance_extensions"]=extensions;
                }
            }
        }
        report["passed"]=status.ok;report["error"]=status.message;report["runtime_error"]=status.runtime_error;
        exit_code=status?0:1;
        bridge.Shutdown();
    } catch(const std::exception& e) {report["passed"]=false;report["error"]=e.what();}
    report["exit_code"]=exit_code;
    const std::string data=report.dump(2)+"\n";
    std::cout<<data;
    if(!output.empty()) {std::ofstream file(output,std::ios::binary|std::ios::trunc);if(!file){std::cerr<<"Cannot write JSON output\n";return 2;}file<<data;if(!file){std::cerr<<"JSON write failed\n";return 2;}}
    return exit_code;
}
