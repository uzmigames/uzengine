"""UE5 Python script to dump actor properties from a level.

Run via: UnrealEditor-Cmd.exe Project.uproject -run=pythonscript -script="dump_level_actors.py"
Or from UE5 Python console.
"""
import unreal
import json

def dump_actor_properties(actor):
    """Extract all editable properties from an actor."""
    result = {}
    result["_class"] = actor.get_class().get_name()
    result["_name"] = actor.get_name()

    # Get actor location
    loc = actor.get_actor_location()
    result["Location"] = {"X": loc.x, "Y": loc.y, "Z": loc.z}
    rot = actor.get_actor_rotation()
    result["Rotation"] = {"Pitch": rot.pitch, "Yaw": rot.yaw, "Roll": rot.roll}
    scale = actor.get_actor_scale3d()
    result["Scale"] = {"X": scale.x, "Y": scale.y, "Z": scale.z}

    # Get components
    components = actor.get_components_by_class(unreal.ActorComponent)
    for comp in components:
        comp_data = {}
        comp_name = comp.get_name()
        comp_class = comp.get_class().get_name()

        # Get all properties via reflection
        for prop in comp.get_class().get_editor_properties():
            try:
                val = getattr(comp, prop, None)
                if val is not None:
                    if isinstance(val, (int, float, str, bool)):
                        comp_data[prop] = val
                    elif isinstance(val, unreal.LinearColor):
                        comp_data[prop] = {"R": val.r, "G": val.g, "B": val.b, "A": val.a}
                    elif isinstance(val, unreal.Color):
                        comp_data[prop] = {"R": val.r, "G": val.g, "B": val.b, "A": val.a}
                    elif isinstance(val, unreal.Vector):
                        comp_data[prop] = {"X": val.x, "Y": val.y, "Z": val.z}
                    elif isinstance(val, unreal.Rotator):
                        comp_data[prop] = {"P": val.pitch, "Y": val.yaw, "R": val.roll}
                    elif hasattr(val, '__str__'):
                        s = str(val)
                        if len(s) < 200:
                            comp_data[prop] = s
            except:
                pass

        result[f"Component:{comp_class}:{comp_name}"] = comp_data

    return result

# Load the level
level_path = "/Game/Level/Basic"
world = unreal.EditorLoadingAndSavingUtils.load_map(level_path)

actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)

# Filter to lighting/atmosphere/fog actors
filter_classes = [
    "ExponentialHeightFog",
    "SkyAtmosphere",
    "SkyLight",
    "DirectionalLight",
    "PostProcessVolume",
]

output = {"level": level_path, "actors": []}

for actor in actors:
    class_name = actor.get_class().get_name()
    if any(f in class_name for f in filter_classes):
        output["actors"].append(dump_actor_properties(actor))

# Write JSON
with open("E:/UzEngine/docs/analysis/basic-level-actors.json", "w") as f:
    json.dump(output, f, indent=2, default=str)

unreal.log(f"Exported {len(output['actors'])} actors to basic-level-actors.json")
