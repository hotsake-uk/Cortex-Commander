"""PNG rendering of the composed material layer, the path grid's view of it, and a path."""

from PIL import Image, ImageDraw

from grid_rules import PathStepKind

AIR_COLOR = (18, 22, 34)
KIND_COLORS = {
    PathStepKind.Walk: (80, 220, 80),
    PathStepKind.Crawl: (240, 220, 60),
    PathStepKind.Jump: (70, 200, 255),
    PathStepKind.Fall: (255, 150, 50),
    PathStepKind.Dig: (190, 120, 60),
    PathStepKind.Door: (230, 80, 230),
}
COURSE_COLORS = [(80, 220, 80), (70, 200, 255), (255, 150, 50), (240, 220, 60), (230, 80, 230), (255, 255, 255), (255, 90, 90), (120, 255, 200)]


def render(scene, crop, scale=2, pathfinder=None, paths=(), grid=False, ground_dots=False, title=None):
    """crop = (x0, y0, x1, y1) in scene pixels. paths = [(points, kinds, color_or_None, label)]: with color None each step is coloured by
    its kind; otherwise the whole path takes that colour."""
    x0, y0, x1, y1 = crop
    width, height = x1 - x0, y1 - y0
    array = scene.to_array()
    palette = [AIR_COLOR] + [(0, 0, 0)] * 255
    for index in range(1, 256):
        palette[index] = scene.materials.by_id[index].color
    image = Image.new("RGB", (width, height), AIR_COLOR)
    pixels = image.load()
    for y in range(y0, y1):
        if y < 0 or y >= scene.h:
            continue
        row = array[y]
        for x in range(x0, x1):
            xx = x % scene.w if scene.wraps_x else x
            if xx < 0 or xx >= scene.w:
                continue
            value = int(row[xx])
            if value:
                pixels[x - x0, y - y0] = palette[value]
    image = image.resize((width * scale, height * scale), Image.NEAREST)
    draw = ImageDraw.Draw(image)

    def to_image(point):
        return ((point[0] - x0) * scale, (point[1] - y0) * scale)

    if grid and pathfinder is not None:
        nd = pathfinder.m_NodeDimension
        for gx in range(x0 - x0 % nd, x1 + 1, nd):
            draw.line([to_image((gx, y0)), to_image((gx, y1))], fill=(40, 46, 64), width=1)
        for gy in range(y0 - y0 % nd, y1 + 1, nd):
            draw.line([to_image((x0, gy)), to_image((x1, gy))], fill=(40, 46, 64), width=1)

    if ground_dots and pathfinder is not None:
        for node in pathfinder.m_NodeGrid:
            px, py = node.Pos
            if x0 <= px < x1 and y0 <= py < y1:
                cx, cy = to_image((px, py))
                if pathfinder.NodeIsOnSolidGround(node):
                    draw.ellipse([cx - 1, cy - 1, cx + 1, cy + 1], fill=(120, 120, 160))
                if node.Surface >= 0:
                    sx, sy = to_image((px, node.Surface))
                    draw.point((sx, sy), fill=(255, 255, 255))

    legend_y = 4
    for points, kinds, color, label in paths:
        for i in range(1, len(points)):
            kind = kinds[i - 1] if i - 1 < len(kinds) else PathStepKind.Walk
            line_color = color if color is not None else KIND_COLORS.get(kind, (255, 255, 255))
            draw.line([to_image(points[i - 1]), to_image(points[i])], fill=line_color, width=max(1, scale))
            cx, cy = to_image(points[i])
            draw.ellipse([cx - scale, cy - scale, cx + scale, cy + scale], outline=line_color)
        if points:
            sx, sy = to_image(points[0])
            draw.rectangle([sx - 2 * scale, sy - 2 * scale, sx + 2 * scale, sy + 2 * scale], outline=(255, 255, 255))
            ex, ey = to_image(points[-1])
            draw.rectangle([ex - 2 * scale, ey - 2 * scale, ex + 2 * scale, ey + 2 * scale], outline=(255, 60, 60))
        if label:
            draw.text((4, legend_y), label, fill=color if color is not None else (255, 255, 255))
            legend_y += 12
    if title:
        draw.text((4, image.height - 14), title, fill=(255, 255, 255))
    if any(color is None for _, _, color, _ in paths):
        kx = image.width - 90
        for kind, name in enumerate(PathStepKind.NAMES):
            draw.rectangle([kx, 4 + kind * 12, kx + 8, 12 + kind * 12], fill=KIND_COLORS[kind])
            draw.text((kx + 12, 2 + kind * 12), name, fill=(255, 255, 255))
    return image
