"""Rebuild the project-owned brand assets. Requires Pillow (build time only)."""

from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
NAVY, SCREEN, CYAN = "#10283f", "#173f58", "#38e4d0"


def icon(size):
    scale = 4
    im = Image.new("RGBA", (256 * scale, 256 * scale))
    d = ImageDraw.Draw(im)

    def box(v):
        return tuple(round(x * scale) for x in v)

    d.rounded_rectangle(box((0, 8, 256, 206)), radius=28 * scale, fill=NAVY)
    d.rounded_rectangle(box((17, 25, 239, 189)), radius=15 * scale, fill=SCREEN)
    d.arc(box((86, 65, 170, 149)), start=-45, end=225, fill=CYAN, width=14 * scale)
    d.line(box((128, 59, 128, 106)), fill=CYAN, width=14 * scale)
    d.ellipse(box((121, 52, 135, 66)), fill=CYAN)
    d.rectangle(box((117, 205, 139, 237)), fill=NAVY)
    d.rounded_rectangle(box((78, 232, 178, 248)), radius=8 * scale, fill=NAVY)
    return im.resize((size, size), Image.Resampling.LANCZOS)


brand = ROOT / "custom_components/tcl_display/brand"
brand.mkdir(exist_ok=True)
for size, suffix in [(256, ""), (512, "@2x")]:
    icon(size).save(brand / f"icon{suffix}.png", optimize=True)
for dark, color in [(False, NAVY), (True, "#e7f3ff")]:
    for size, suffix in [(256, ""), (512, "@2x")]:
        im = Image.new("RGBA", (size * 4, size))
        im.alpha_composite(icon(size), (0, 0))
        font = ImageFont.truetype("C:/Windows/Fonts/segoeuib.ttf", round(size * 0.38))
        ImageDraw.Draw(im).text(
            (round(size * 1.12), round(size * 0.23)), "TCL Display", font=font, fill=color
        )
        im.save(brand / f"{'dark_' if dark else ''}logo{suffix}.png", optimize=True)
icon(512).save(ROOT / "assets/icon.png", optimize=True)
icon(256).save(ROOT / "windows/src/app.ico", sizes=[(n, n) for n in [16, 20, 24, 32, 40, 48, 64, 128, 256]])
(ROOT / "assets/icon.svg").write_text(
    """<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256"><rect y="8" width="256" height="198" rx="28" fill="#10283f"/><rect x="17" y="25" width="222" height="164" rx="15" fill="#173f58"/><path d="M157.7 77.3a42 42 0 1 1-59.4 0M128 59v47" fill="none" stroke="#38e4d0" stroke-width="14" stroke-linecap="round"/><path d="M117 206h22v31h-22z" fill="#10283f"/><rect x="78" y="232" width="100" height="16" rx="8" fill="#10283f"/></svg>""",
    encoding="utf-8",
)
print("Created native multi-size ICO and local HA light/dark PNG assets.")
