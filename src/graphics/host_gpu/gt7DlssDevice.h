#pragma once

#include "graphics/host_gpu/vulkanCommon.h"
#include "../../../tools/dlss_backend/kyty_dlss_backend.h"
#include <vector>

namespace Libs::Graphics {
struct GraphicContext;
namespace Gt7DlssHost {
bool Requested();
const KytyDlssApi* Api();
const KytyDlssPaths& Paths();
void AddInstanceExtensions(std::vector<const char*>& required,
                           const std::vector<vk::ExtensionProperties>& available);
void AddDeviceExtensions(GraphicContext& graphics, std::vector<const char*>& required);
} // namespace Gt7DlssHost
} // namespace Libs::Graphics
