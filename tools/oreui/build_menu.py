"""Generate version-locked LHolo menu assets from a reviewed installed gameplay bundle.

Never overwrite vanilla assets. Generated menu resources ship with the complete mod.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

EXPECTED_SHA256 = "9e7efe423b6cc8a1c1fa92756bd181675cffa51a4728bce386b0081cad1bb979"
PROJECT = Path(__file__).resolve().parents[2]


def replace_once(source: str, anchor: str, replacement: str) -> str:
    if source.count(anchor) != 1:
        raise ValueError(f"Unsupported gameplay anchor: {anchor[:70]}")
    return source.replace(anchor, replacement, 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("hbui", type=Path)
    parser.add_argument("--output", type=Path, default=PROJECT / "build/oreui")
    args = parser.parse_args()
    html = (args.hbui / "gameplay.html").read_text(encoding="utf-8")
    scripts = re.findall(r'<script\b[^>]*\bsrc="([^"]+)"', html)
    if len(scripts) != 1:
        raise ValueError("Expected exactly one gameplay script")
    raw = (args.hbui / Path(scripts[0]).name).read_bytes()
    if hashlib.sha256(raw).hexdigest() != EXPECTED_SHA256:
        raise ValueError("Unsupported gameplay bundle hash; review the version adapter first")
    subprocess.run(["npm.cmd", "run", "build"], cwd=PROJECT / "frontend", check=True)
    bundle = raw.decode("utf-8")
    if bundle.count('function dx({itemID:t,width:n,height:r,floating:a,hideShadow:o,bag:l})') != 1:
        raise ValueError("Unsupported native item icon component; review the runtime adapter")
    if bundle.count('"e96d2bbd1477d4e72350"') != 1 or bundle.count('`geo-item://${e}?width=${t}&height=${n}&bag=${encodeURIComponent(null!=l?l:"")}`') != 1:
        raise ValueError("Unsupported native thumbnail markup; review the runtime adapter")
    start_anchor = "function CG(){"
    end_anchor = '(function(e,t){if(null===t)return()=>{};Fw(t).render(e)})(e.createElement(CG,null)'
    if bundle.count(start_anchor) != 1 or bundle.count(end_anchor) != 1:
        raise ValueError("Unsupported gameplay application entry")
    start, end = bundle.index(start_anchor), bundle.index(end_anchor)
    adapter = Path(__file__).with_name("runtime_adapter.js").read_text(encoding="utf-8")
    app = (PROJECT / "frontend/dist/app.js").read_text(encoding="utf-8")
    entry = adapter + "\n" + app + "\nfunction CG(){return e.createElement(LHoloOreUi.App,null);}\n"
    result = bundle[:start] + entry + bundle[end:]
    result = replace_once(result, 'e.createElement(Go,{driver:cw}',
                          'e.createElement(Go,{driver:LHoloUseMenuHistory}')
    result = '// lholo-menu-v7-settings\n' + result
    html = replace_once(html, scripts[0], "/hbui/lholo-menu.js")
    css = (PROJECT / "frontend/dist/menu.css").read_text(encoding="utf-8")
    html = replace_once(html, "</head>", "<style>" + css + "</style></head>")
    html = replace_once(html, '<body>', '<body><!-- lholo-menu-v7-settings -->')
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "lholo-menu.js").write_text(result, encoding="utf-8")
    (args.output / "lholo-menu.html").write_text(html, encoding="utf-8")
    (args.output / "build-info.json").write_text(json.dumps({
        "sourceSha256": EXPECTED_SHA256, "route": "/lholo-menu", "gameVersion": "1.26.60.05",
    }, indent=2), encoding="utf-8")
    print(f"Generated menu assets: {args.output.resolve()}")


if __name__ == "__main__":
    main()
