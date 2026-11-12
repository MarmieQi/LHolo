#pragma once

#include <string_view>

namespace lholo::oreui {
inline constexpr std::string_view kActionHintPage = R"(<!doctype html><meta charset="utf-8"><style>
@font-face{font-family:LHoloSans;src:url('/hbui/fonts/NotoSansSC-Regular-a4317c614d07055b56ed.otf')}
html,body{width:100%;height:100%;margin:0;background:transparent;pointer-events:none}
#anchor{position:absolute;left:16px;right:16px;bottom:20%;display:flex;flex-direction:row;justify-content:center}
#hint{display:none;max-width:100%;box-sizing:border-box;background:rgba(24,24,24,.78);color:#fff;border-radius:5px;padding:8px 16px;font:24px LHoloSans,sans-serif;line-height:1.35;text-align:center}
</style><div id="anchor"><div id="hint"></div></div><script>
const hint = document.getElementById('hint');
let fadeTimer;
window.engine.on('lholo:action-hint', payload => {
    const {text, remainingMs, persistent} = JSON.parse(payload);
    clearTimeout(fadeTimer);
    hint.style.transition = 'none';
    hint.style.opacity = '1';
    hint.textContent = text;
    hint.style.display = text && (persistent || remainingMs > 0) ? 'block' : 'none';
    if (text && !persistent && remainingMs > 0) {
        const fade = Math.min(300, remainingMs);
        fadeTimer = setTimeout(() => {
            hint.style.transition = 'opacity ' + fade + 'ms linear';
            hint.style.opacity = '0';
        }, remainingMs - fade);
    }
});
requestAnimationFrame(() => {
    window.engine.trigger('core:telemetry:firstContentfulPaint');
    window.engine.trigger('core:telemetry:firstMeaningfulPaint');
});
</script>)";
}
