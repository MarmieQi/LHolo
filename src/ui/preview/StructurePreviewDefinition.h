#pragma once

namespace lholo::ui::preview {
// Minimal native viewport: no decorative frames or separate layout editor.
inline constexpr char kStructurePreviewDefinition[] = R"json({
  "type":"screen","render_game_behind":true,"is_showing_menu":true,
  "is_modal":true,"absorbs_input":true,"should_steal_mouse":false,
  "low_frequency_rendering":false,
  "controls":[
    {"preview_backdrop":{"type":"custom","renderer":"gradient_renderer","layer":0,
      "color1":[0,0,0,0],"color2":[0,0,0,0]}},
    {"preview_frame":{"type":"panel","size":["93%","72%"],
      "anchor_from":"center","anchor_to":"center","layer":1,"controls":[
    {"lholo_preview_canvas":{"type":"custom","renderer":"3d_structure_renderer","layer":1,
      "size":["92%","80%"],"anchor_from":"center","anchor_to":"center",
      "property_bag":{"#has_focus":true,"#bottom_left_block":[0,0,0],"#top_right_block":[1,1,1],
        "#include_entities":false,"#include_players":false,"#remove_blocks":false}}}
    ]}},
    {"cursor":{"type":"custom","size":[16,16],"renderer":"cursor_renderer","layer":1000}}
  ],
  "button_mappings":[{"from_button_id":"button.menu_cancel","to_button_id":"button.lholo_preview_close","mapping_type":"global"}]
})json";
}
