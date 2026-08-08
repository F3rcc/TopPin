from pathlib import Path
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[1]
SVG_PATH = ROOT / "Assets" / "TopPin.svg"
ICO_PATH = ROOT / "Assets" / "TopPin.ico"
PREVIEW_PATH = ROOT / "Assets" / "TopPin-icon-preview.png"
NAMESPACE = "{http://www.w3.org/2000/svg}"


def scale(value: str, factor: int) -> int:
    return round(float(value) * factor)


def render_icon(size: int = 256, supersample: int = 4) -> Image.Image:
    factor = size * supersample // 256
    canvas = Image.new("RGBA", (256 * factor, 256 * factor), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    root = ET.parse(SVG_PATH).getroot()

    for element in root:
        fill = element.attrib["fill"]
        if element.tag == f"{NAMESPACE}rect":
            x = scale(element.attrib["x"], factor)
            y = scale(element.attrib["y"], factor)
            width = scale(element.attrib["width"], factor)
            height = scale(element.attrib["height"], factor)
            radius = scale(element.attrib.get("rx", "0"), factor)
            draw.rounded_rectangle(
                (x, y, x + width, y + height),
                radius=radius,
                fill=fill,
            )
        elif element.tag == f"{NAMESPACE}polygon":
            points = []
            for pair in element.attrib["points"].split():
                x, y = pair.split(",")
                points.append((scale(x, factor), scale(y, factor)))
            draw.polygon(points, fill=fill)

    return canvas.resize((size, size), Image.Resampling.LANCZOS)


def main() -> None:
    preview = render_icon()
    PREVIEW_PATH.parent.mkdir(parents=True, exist_ok=True)
    preview.save(PREVIEW_PATH, optimize=True)
    preview.save(
        ICO_PATH,
        format="ICO",
        sizes=[(16, 16), (20, 20), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)],
    )
    print(f"Wrote {ICO_PATH} ({ICO_PATH.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
