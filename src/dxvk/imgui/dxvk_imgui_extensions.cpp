#include "dxvk_imgui_extensions.h"
#include "../rtx_render/rtx_gui_widgets.h"

namespace ImGui {

  void ImGui::SetTooltipToLastWidgetOnHover(const char* fmt, ...) {
    // NV-DXVK start: same hover delay as the RemixGui tooltips; the arguments go through SetTooltipV
    if (ImGui::IsItemHovered() && RemixGui::TooltipDelayElapsed(GImGui->LastItemData.ID)) {
      va_list args;
      va_start(args, fmt);
      SetTooltipV(fmt, args);
      va_end(args);
    }
    // NV-DXVK end
  }
}
