"""PathAgent (Source/System/PathFinder.h) and the Soldier Light's values.

Actor::GetPathAgent (Source/Entities/Actor.cpp 1096-1107) and AHuman::GetPathAgent (AHuman.cpp 1012-1016):
    JumpHeight     = EstimateJumpHeight()           AHuman.cpp 1018-1077 (ported below)
    DigStrength    = EstimateDigStrength()          AIBaseDigStrength (35, c_PathFindingDefaultDigStrength) unless a digger is carried
    BreachStrength = EstimateBreachStrength()       max(dig, rifle AIPenetration * 0.9); only matters for door material (none in these modules)
    StandHeight    = max(16, CharHeight * 0.42)     Soldier Light CharHeight = 100 (Coalition.rte/.../CoalitionLight.ini 628) -> 42
    CrawlHeight    = max(12, CharHeight * 0.24)     -> 24
    HalfWidth      = clamp(GetRadius() * 0.5, 8, 16)
        GetRadius is MOSRotating's: max(sprite radius, farthest attachable distance + its radius), the attachable distances taken
        when they are added (MOSRotating::AddAttachable -> HandlePotentialRadiusAffectingAttachable, MOSRotating.cpp 1568/1977;
        an attachable's own radius propagates to its parent, Attachable.cpp 346-376).
          torso sprite alone (TorsoA.png 12x17, SpriteOffset -7,-12): 13.9                      -> HalfWidth 8 (the clamp floor)
          head: |ParentOffset (0,-10) - JointOffset (-2,5)| = 15.1 + HeadA 12x12 radius 9.2 = 24.4 -> 12.2
          legs: |(1,1) - (-5,2)| = 6.1 + leg radius (LegFGA 17x12 -> 13.0, or the foot: |(-11,-10)| + 6.5 ~ 21.4) ~ 27.5 -> 13.8
          FG arm with the Assault Rifle: |(-2,-6) - (-3.5,-1)| = 5.2 + arm radius, where the arm's farthest attachable is the rifle
            at the hand (IdleOffset (4,9), rifle JointOffset (-4,3): |(8,6)| = 10) + rifle radius (25x12, SpriteOffset -13,-5: 14.8)
            = 24.8  ->  30.0  -> HalfWidth 15.0
        So an armed Soldier Light has HalfWidth ~15 (13.8 without the rifle's reach, 12.2 bare). The default here is 15. It is a
        knife edge: Shaft A and Hub A have 48 px openings, a node in them has ClearLeft + ClearRight = 46, and the jet column wants
        3 * HalfWidth (RoomToPass(node, 3.0)): 45 passes, 48 (HalfWidth 16) is refused.

The Lua AIBUNKER "path for" line calls Scene:CalculatePath(start, end, jumpHeight, 35, team), which builds a PathAgent with the
header defaults: StandHeight 40, CrawlHeight 22, HalfWidth 6, BreachStrength -1 (= dig). That is `lua_default_agent()`.
"""

import math

FLT_MAX = 3.4028234663852886e38
C_PPM = 20.0
C_MPP = 1.0 / C_PPM
C_PATHFINDING_DEFAULT_DIG_STRENGTH = 35.0


class PathAgent:
    def __init__(self, JumpHeight=FLT_MAX, DigStrength=35.0, BreachStrength=-1.0, StandHeight=40.0, CrawlHeight=22.0, HalfWidth=6.0):
        self.JumpHeight = JumpHeight
        self.DigStrength = DigStrength
        self.BreachStrength = BreachStrength
        self.StandHeight = StandHeight
        self.CrawlHeight = CrawlHeight
        self.HalfWidth = HalfWidth

    def __repr__(self):
        return "PathAgent(jump %s m, dig %g, breach %g, stand %g, crawl %g, halfwidth %g)" % (
            "inf" if self.JumpHeight == FLT_MAX else "%.2f" % self.JumpHeight, self.DigStrength, self.BreachStrength, self.StandHeight, self.CrawlHeight, self.HalfWidth)


SOLDIER_LIGHT_CHAR_HEIGHT = 100.0

# The Soldier Light's jetpack: "Jetpack Heavy" (Base.rte/Actors/Shared.ini 705-718) on top of "Jetpack" (612-690), with the soldier's own
# overrides (CoalitionLight.ini 640-648): ParticlesPerMinute 12000, JumpTime 1.5 s. Four emissions in all, each pushing the emitter:
#   (spread, min velocity, max velocity, particle mass) -- Jetpack Blast 1/2 and Jetpack Blast Ball 1 all have Mass 1.25.
SOLDIER_LIGHT_JET_EMISSIONS = [
    (0.1, 12.0, 23.0, 1.25),  # Jetpack Blast 1 (base)
    (0.2, 12.0, 23.0, 1.25),  # Jetpack Blast 2 (base)
    (0.15, 5.0, 25.0, 1.25),  # Jetpack Blast Ball 1 (base)
    (0.25, 10.0, 26.0, 1.25),  # Jetpack Blast 1 (Jetpack Heavy's extra emission)
]
SOLDIER_LIGHT_JET_PPM_TOTAL = 12000.0  # AEmitter "ParticlesPerMinute" is split evenly over the emissions (AEmitter.cpp 129-136)
SOLDIER_LIGHT_JET_BURST_SIZE_TOTAL = 8  # "BurstSize" 8 from Jetpack Heavy: ceil(8 / 4) = 2 per emission (AEmitter.cpp 141-148)
SOLDIER_LIGHT_JET_BURST_SCALE = 2.0  # base Jetpack
SOLDIER_LIGHT_JET_TIME_MS = 1500.0  # JumpTime 1.5 s (AEJetpack.cpp 86-89 stores it in ms)
SOLDIER_LIGHT_JET_THROTTLE_FACTOR = 1.0  # Lerp(-1, 1, 0.7, 1.3, throttle 0) (AEmitter.h 143)

# AHuman::GetMass = torso + attachables (+ their attachables) + inventory. Soldier Light: torso 48, head 32 + helmet 4, arms 8 + 4,
# legs 12 + 12 (Leg BG copies Leg FG) with feet 4 + 4, jetpack 1, Assault Rifle 11 (+ a magazine, ~1). About 142 kg; a parameter here.
SOLDIER_LIGHT_MASS_ESTIMATE = 142.0

DELTA_TIME_S = 0.0166666  # c_DefaultDeltaTimeS (Constants.h 32)
DELTA_TIME_MS = DELTA_TIME_S * 1000.0
GLOBAL_ACC_Y = 20.0  # Ketanot Hills GlobalAcceleration (and the Scene default)


def jet_estimate_impulse(burst, emissions=SOLDIER_LIGHT_JET_EMISSIONS, ppm_total=SOLDIER_LIGHT_JET_PPM_TOTAL,
                         burst_size_total=SOLDIER_LIGHT_JET_BURST_SIZE_TOTAL, burst_scale=SOLDIER_LIGHT_JET_BURST_SCALE,
                         throttle_factor=SOLDIER_LIGHT_JET_THROTTLE_FACTOR):
    """AEmitter::EstimateImpulse (Source/Entities/AEmitter.cpp 272-321)."""
    count = len(emissions)
    ppm = ppm_total / count
    burst_size = math.ceil(burst_size_total / count)
    impulse = 0.0
    for spread, vel_min, vel_max, mass in emissions:
        emissions_per_frame = (ppm / 60.0) * DELTA_TIME_S
        scale = 1.0
        emissions_per_frame *= 1  # particle count
        if burst:
            emissions_per_frame += burst_size
            scale = burst_scale
        v_min = vel_min * scale
        v_range = (vel_max - vel_min) * scale * 0.5
        spread_factor = max(math.pi - (spread * scale), 0.0) / math.pi
        impulse += (v_min + v_range) * spread_factor * mass * emissions_per_frame
    return impulse * throttle_factor


def estimate_jump_height(total_mass=SOLDIER_LIGHT_MASS_ESTIMATE, jet_time_ms=SOLDIER_LIGHT_JET_TIME_MS, gravity=GLOBAL_ACC_Y, **jet):
    """AHuman::EstimateJumpHeight (Source/Entities/AHuman.cpp 1018-1077): a frame-by-frame climb on a full tank, in metres."""
    current_y_velocity = 0.0
    y_gravity = -(gravity * DELTA_TIME_S)
    impulse_burst = jet_estimate_impulse(True, **jet) / total_mass
    impulse_thrust = jet_estimate_impulse(False, **jet) / total_mass
    fuel_time = jet_time_ms
    fuel_use_thrust = jet.get("throttle_factor", SOLDIER_LIGHT_JET_THROTTLE_FACTOR)
    fuel_use_burst = fuel_use_thrust * impulse_burst / impulse_thrust
    has_bursted = False
    total_height = 0.0
    while True:
        if not has_bursted and fuel_time > 0.0:
            current_y_velocity += impulse_burst
            fuel_time -= (1000.0 / 60.0) * fuel_use_burst
            has_bursted = True
        if fuel_time > 0.0:
            current_y_velocity += impulse_thrust
            fuel_time -= DELTA_TIME_MS * fuel_use_thrust
        if current_y_velocity + y_gravity >= current_y_velocity:
            return FLT_MAX
        current_y_velocity += y_gravity
        if current_y_velocity > 0.0:
            total_height += current_y_velocity * DELTA_TIME_S * C_PPM
        else:
            break
    return total_height * C_MPP


SOLDIER_LIGHT_HALF_WIDTH = 14.0  # The game prints "radius 28" for the Soldier Light (Results/7f09496): GetRadius 28 -> 14.
SOLDIER_LIGHT_JUMP_HEIGHT_M = 22.0  # The game prints "jump height 440 px" for it; the EstimateJumpHeight port below gives less (--jump auto).


def soldier_light_agent(jump_height=None, half_width=SOLDIER_LIGHT_HALF_WIDTH, dig_strength=C_PATHFINDING_DEFAULT_DIG_STRENGTH, breach_strength=None, mass=SOLDIER_LIGHT_MASS_ESTIMATE):
    """The Soldier Light's PathAgent as AHuman::GetPathAgent builds it. jump_height None -> the EstimateJumpHeight port."""
    if jump_height is None:
        jump_height = SOLDIER_LIGHT_JUMP_HEIGHT_M
    elif jump_height == "auto":
        jump_height = estimate_jump_height(mass)
    # AHuman::GetPathAgent: the standing body is 0.44 of the height (feet a fifth under Pos, head top about a quarter over it), crawl 0.24.
    stand = max(16.0, SOLDIER_LIGHT_CHAR_HEIGHT * 0.44)
    crawl = max(12.0, SOLDIER_LIGHT_CHAR_HEIGHT * 0.24)
    return PathAgent(JumpHeight=jump_height, DigStrength=dig_strength, BreachStrength=dig_strength if breach_strength is None else breach_strength,
                     StandHeight=stand, CrawlHeight=crawl, HalfWidth=max(8.0, min(16.0, half_width)))


def lua_default_agent(jump_height=None, dig_strength=C_PATHFINDING_DEFAULT_DIG_STRENGTH, mass=SOLDIER_LIGHT_MASS_ESTIMATE):
    """What Scene:CalculatePath(start, end, jumpHeight, digStrength, team) from Lua uses: the PathAgent header defaults."""
    if jump_height is None:
        jump_height = estimate_jump_height(mass)
    return PathAgent(JumpHeight=jump_height, DigStrength=dig_strength, BreachStrength=-1.0)
