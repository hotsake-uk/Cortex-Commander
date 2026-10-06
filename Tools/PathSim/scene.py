"""The scene's material layer and the SceneMan / SLTerrain / TerrainObject / Sandbox behaviour the path grid depends on.

Everything here mirrors game code; each function names what it mirrors:
    GetTerrMatter            SceneMan::GetTerrMatter            Source/Managers/SceneMan.cpp 379-408
    WrapPosition             SceneLayerImpl::WrapPosition       Source/Entities/SceneLayer.cpp 357-379
    ForceBounds              SceneLayerImpl::ForceBounds        Source/Entities/SceneLayer.cpp 381-425, SceneMan.cpp 2333-2345 (Vector)
    ShortestDistance         SceneMan::ShortestDistance         Source/Managers/SceneMan.cpp 2377-2411
    CastMaxStrengthRayMaterial  SceneMan::CastMaxStrengthRayMaterial  Source/Managers/SceneMan.cpp 1577-1655
    CastNotMaterialRay       SceneMan::CastNotMaterialRay       Source/Managers/SceneMan.cpp 1376-1466
    FindAltitude             SceneMan::FindAltitude             Source/Managers/SceneMan.cpp 2261-2282
    MovePointToGround        SceneMan::MovePointToGround        Source/Managers/SceneMan.cpp 2302-2319
    place_structure          Sandbox::StructureCorner/StructurePosition/PlaceStructure  Source/Managers/Sandbox.cpp 1654-1685, Sandbox::Do 4873-4965
                             TerrainObject::Create (BitmapOffset)  Source/Entities/TerrainObject.cpp 34-48
                             TerrainObject::DrawToTerrain       Source/Entities/TerrainObject.cpp 283-345 (draw_sprite: palette index 0 is the mask, not drawn)
                             SceneMan::AddSceneObject (CleanAirBox)  Source/Managers/SceneMan.cpp 2582-2597
    CleanAirBox              SLTerrain::CleanAirBox             Source/Entities/SLTerrain.cpp 624-657 (cavity, index 1, becomes air)
"""

import math

from materials import MATERIAL_AIR, MATERIAL_CAVITY

FLT_MAX = 3.4028234663852886e38


def c_round(value):
    """std::round: half away from zero (Python's round() is half to even)."""
    return math.floor(abs(value) + 0.5) * (1 if value >= 0 else -1)


class Box:
    """System/Box.h: a corner and a size. Unflip() makes the size positive."""

    def __init__(self, corner_x, corner_y, width, height):
        self.x = float(corner_x)
        self.y = float(corner_y)
        self.w = float(width)
        self.h = float(height)

    def Unflip(self):
        if self.w < 0:
            self.x += self.w
            self.w = -self.w
        if self.h < 0:
            self.y += self.h
            self.h = -self.h

    def __repr__(self):
        return "Box(%g,%g %gx%g)" % (self.x, self.y, self.w, self.h)


class Scene:
    """A material layer: width x height bytes of material ids, with the scene's wrapping."""

    def __init__(self, width, height, materials, wraps_x=True, wraps_y=False):
        self.w = int(width)
        self.h = int(height)
        self.materials = materials
        self.wraps_x = wraps_x
        self.wraps_y = wraps_y
        self.mat = bytearray(self.w * self.h)
        self.updated_material_areas = []  # SLTerrain::m_UpdatedMaterialAreas, a deque of Boxes (not wrapped, may be out of bounds)

    @classmethod
    def from_array(cls, array, materials, wraps_x=True, wraps_y=False):
        scene = cls(array.shape[1], array.shape[0], materials, wraps_x, wraps_y)
        scene.mat = bytearray(array.astype("uint8").tobytes())
        return scene

    def to_array(self):
        import numpy as np
        return np.frombuffer(bytes(self.mat), dtype="uint8").reshape(self.h, self.w)

    # SceneMan::GetSceneWidth / GetSceneHeight / SceneWrapsX / SceneWrapsY
    def GetSceneWidth(self):
        return self.w

    def GetSceneHeight(self):
        return self.h

    def SceneWrapsX(self):
        return self.wraps_x

    def SceneWrapsY(self):
        return self.wraps_y

    # --- SceneLayerImpl::WrapPosition (ints) ---------------------------------------------------------------------------------
    def WrapPosition(self, x, y):
        if self.wraps_x:
            x = x % self.w  # C's % then "+= width if < 0" equals Python's % for ints.
        if self.wraps_y:
            y = y % self.h
        return x, y

    # --- SceneMan::GetTerrMatter --------------------------------------------------------------------------------------------------
    def GetTerrMatter(self, x, y):
        """The material id at a pixel: wrapped on wrapping axes, air off the bitmap (above, below and to the sides). Liquids are
        ignored (SceneMan's LiquidsPassable rule; there are none in a bunker)."""
        if self.wraps_x:
            x = x % self.w
        if self.wraps_y:
            y = y % self.h
        if x < 0 or x >= self.w or y >= self.h or y < 0:
            return MATERIAL_AIR
        return self.mat[y * self.w + x]

    # --- SceneLayerImpl::ForceBounds / SceneMan::ForceBounds(Vector&) -----------------------------------------------------------
    def ForceBoundsInt(self, x, y):
        if x < 0:
            if self.wraps_x:
                while x < 0:
                    x += self.w
            else:
                x = 0
        if y < 0:
            if self.wraps_y:
                while y < 0:
                    y += self.h
            else:
                y = 0
        if x >= self.w:
            x = x % self.w if self.wraps_x else self.w - 1
        if y >= self.h:
            y = y % self.h if self.wraps_y else self.h - 1
        return x, y

    def ForceBounds(self, pos):
        """SceneMan::ForceBounds(Vector&): the integer part is bounded, the fraction kept."""
        px, py = pos
        ix, iy = math.floor(px), math.floor(py)
        bx, by = self.ForceBoundsInt(ix, iy)
        return (bx + (px - ix), by + (py - iy))

    # --- SceneMan::ShortestDistance -----------------------------------------------------------------------------------------------
    def ShortestDistance(self, pos1, pos2):
        dx = pos2[0] - pos1[0]
        dy = pos2[1] - pos1[1]
        if self.wraps_x:
            if dx > 0:
                if dx > self.w / 2:
                    dx -= self.w
            else:
                if abs(dx) > self.w / 2:
                    dx += self.w
        if self.wraps_y:
            if dy > 0:
                if dy > self.h / 2:
                    dy -= self.h
            else:
                if abs(dy) > self.h / 2:
                    dy += self.h
        return (dx, dy)

    # --- SceneMan::CastMaxStrengthRayMaterial -------------------------------------------------------------------------------------
    def CastMaxStrengthRayMaterial(self, start, end, skip=0, ignore_material=MATERIAL_AIR):
        """The strongest material on the Bresenham line from start to end. The start pixel itself is never sampled (the position
        steps before the first check); the end pixel always is. Air (and ignore_material) never count. Ties keep the first found
        (strict '>'). Returns the Air material when nothing else is on the line."""
        materials = self.materials
        rx, ry = self.ShortestDistance(start, end)
        strongest = materials.air
        strongest_integrity = strongest.integrity

        pos = [math.floor(start[0]), math.floor(start[1])]
        delta = [math.floor(start[0] + rx) - pos[0], math.floor(start[1] + ry) - pos[1]]
        if delta[0] == 0 and delta[1] == 0:
            return strongest
        increment = [1, 1]
        if delta[0] < 0:
            increment[0] = -1
            delta[0] = -delta[0]
        if delta[1] < 0:
            increment[1] = -1
            delta[1] = -delta[1]
        delta2 = [delta[0] << 1, delta[1] << 1]
        if delta[0] > delta[1]:
            dom, sub = 0, 1
        else:
            dom, sub = 1, 0
        error = delta2[sub] - delta[dom]
        skipped = skip
        dom_steps_total = delta[dom]
        mat = self.mat
        w = self.w
        h = self.h
        wraps_x = self.wraps_x
        wraps_y = self.wraps_y
        for dom_steps in range(dom_steps_total):
            pos[dom] += increment[dom]
            if error >= 0:
                pos[sub] += increment[sub]
                error -= delta2[dom]
            error += delta2[sub]
            skipped += 1
            if skipped > skip or dom_steps + 1 == dom_steps_total:
                # g_SceneMan.WrapPosition(intPos) then GetTerrMatter (which wraps again; harmless).
                if wraps_x:
                    pos[0] %= w
                if wraps_y:
                    pos[1] %= h
                x, y = pos[0], pos[1]
                if x < 0 or x >= w or y < 0 or y >= h:
                    material_id = MATERIAL_AIR
                else:
                    material_id = mat[y * w + x]
                if material_id != MATERIAL_AIR and material_id != ignore_material:
                    found = materials.by_id[material_id]
                    if found.integrity > strongest_integrity:
                        strongest = found
                        strongest_integrity = found.integrity
                skipped = 0
        return strongest

    # --- SceneMan::CastNotMaterialRay (the bool overload, then the float one) -------------------------------------------------
    def CastNotMaterialRay(self, start, ray, material, skip=0):
        """Distance from start to the first pixel along the ray that is not `material` (sampling every skip+1 pixels, and the last
        pixel), or -1 for none."""
        pos = [math.floor(start[0]), math.floor(start[1])]
        delta = [math.floor(start[0] + ray[0]) - pos[0], math.floor(start[1] + ray[1]) - pos[1]]
        if delta[0] == 0 and delta[1] == 0:
            return -1.0
        increment = [1, 1]
        if delta[0] < 0:
            increment[0] = -1
            delta[0] = -delta[0]
        if delta[1] < 0:
            increment[1] = -1
            delta[1] = -delta[1]
        delta2 = [delta[0] << 1, delta[1] << 1]
        if delta[0] > delta[1]:
            dom, sub = 0, 1
        else:
            dom, sub = 1, 0
        error = delta2[sub] - delta[dom]
        skipped = skip
        total = delta[dom]
        for dom_steps in range(total):
            pos[dom] += increment[dom]
            if error >= 0:
                pos[sub] += increment[sub]
                error -= delta2[dom]
            error += delta2[sub]
            skipped += 1
            if skipped > skip or dom_steps + 1 == total:
                x, y = self.WrapPosition(pos[0], pos[1])
                if self.GetTerrMatter(x, y) != material:
                    # result.SetXY(intPos); result -= start; return result.GetMagnitude()
                    return math.hypot(x - start[0], y - start[1])
                skipped = 0
        return -1.0

    # --- SceneMan::FindAltitude ----------------------------------------------------------------------------------------------------------
    def FindAltitude(self, from_pos, maximum, accuracy):
        temp = self.ForceBounds(from_pos)
        y_dir = float(maximum if maximum > 0 else self.h)
        result = self.CastNotMaterialRay(temp, (0.0, y_dir), MATERIAL_AIR, accuracy)
        if result < 0:
            result = float(maximum if maximum > 0 else self.h)
        return result

    # --- SceneMan::MovePointToGround -------------------------------------------------------------------------------------------------
    def MovePointToGround(self, from_pos, height_above_ground, accuracy, max_distance=0):
        temp = self.ForceBounds(from_pos)
        altitude = self.FindAltitude(temp, self.h, accuracy)
        if altitude == self.h or (max_distance != 0 and altitude > max_distance):
            return temp
        return (temp[0], temp[1] + (altitude - height_above_ground))

    # --- SLTerrain::AddUpdatedMaterialArea ---------------------------------------------------------------------------------------------
    def AddUpdatedMaterialArea(self, box):
        self.updated_material_areas.append(box)

    # --- Allegro draw_sprite onto the material bitmap: index 0 (the mask colour) is skipped ----------------------------------------
    def draw_sprite(self, bitmap, x, y):
        height, width = bitmap.shape
        for by in range(height):
            ty = y + by
            if ty < 0 or ty >= self.h:
                continue
            row = bitmap[by]
            base = ty * self.w
            for bx in range(width):
                tx = x + bx
                if tx < 0 or tx >= self.w:
                    continue
                value = int(row[bx])
                if value != 0:
                    self.mat[base + tx] = value

    # --- SLTerrain::CleanAirBox ----------------------------------------------------------------------------------------------------------
    def CleanAirBox(self, box, wraps_x, wraps_y):
        width, height = self.w, self.h
        for y in range(math.floor(box.y), int(box.y + box.h)):
            for x in range(math.floor(box.x), int(box.x + box.w)):
                wx, wy = x, y
                if wraps_x:
                    if wx < 0:
                        wx += width
                    if wx >= width:
                        wx -= width
                if wraps_y:
                    if wy < 0:
                        wy += height
                    if wy >= height:
                        wy -= height
                if 0 <= wx < width and 0 <= wy < height:
                    index = wy * width + wx
                    if self.mat[index] == MATERIAL_CAVITY:
                        self.mat[index] = MATERIAL_AIR

    # --- TerrainObject::DrawToTerrain --------------------------------------------------------------------------------------------------
    def draw_terrain_object(self, bitmap, pos_on_scene):
        """Draws a TerrainObject's material bitmap with its top-left corner at pos_on_scene (= m_Pos + m_BitmapOffset), with the
        wrap-around copies DrawToTerrain makes near the seams, and registers the updated area."""
        height, width = bitmap.shape
        fx, fy = math.floor(pos_on_scene[0]), math.floor(pos_on_scene[1])
        if self.wraps_x:
            if fx < 0:
                self.draw_sprite(bitmap, fx + self.w, fy)
            elif fx >= self.w - width:
                self.draw_sprite(bitmap, fx - self.w, fy)
        if self.wraps_y:
            if fy < 0:
                self.draw_sprite(bitmap, fx, fy + self.h)
            elif fy >= self.h - height:
                self.draw_sprite(bitmap, fx, fy - self.h)
        self.draw_sprite(bitmap, fx, fy)
        self.AddUpdatedMaterialArea(Box(pos_on_scene[0], pos_on_scene[1], width, height))

    # --- The sandbox's "Structure" tool: Sandbox::Do -> PlaceStructure -> StructurePosition -> TerrainObject placed by SceneMan::AddSceneObject
    def place_structure(self, bitmap, centre_x, centre_y, snap=True):
        """Places a bunker module bitmap by its centre the way SandboxDo("Structure", Vector(x, y), ...) does.

        Sandbox::Do sets stroke.Count = 1 for a Structure, and PlaceStructure snaps when Count > 0: the top-left corner
        (centre - size/2) is rounded to the 24 px grid. StructurePosition then subtracts the TerrainObject's BitmapOffset
        (-(w/2), -(h/2), integer halves, for bitmaps over 24 px) to get m_Pos; DrawToTerrain draws at m_Pos + BitmapOffset, i.e.
        at the snapped corner. Then AddSceneObject cleans cavity (index 1) to air inside the bitmap's box.
        Returns (m_Pos, top-left corner)."""
        height, width = bitmap.shape
        offset_x = -float(width // 2) if width > 24 else 0.0
        offset_y = -float(height // 2) if height > 24 else 0.0
        top_left_x = centre_x - width * 0.5
        top_left_y = centre_y - height * 0.5
        if snap:
            top_left_x = c_round(top_left_x / 24.0) * 24.0
            top_left_y = c_round(top_left_y / 24.0) * 24.0
        pos = (top_left_x - offset_x, top_left_y - offset_y)
        pos_on_scene = (pos[0] + offset_x, pos[1] + offset_y)
        self.draw_terrain_object(bitmap, pos_on_scene)
        self.CleanAirBox(Box(pos_on_scene[0], pos_on_scene[1], width, height), self.wraps_x, self.wraps_y)
        return pos, pos_on_scene
