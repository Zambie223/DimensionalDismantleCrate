"""Loads /DimensionalDismantleCrate/DDCConfiguration in a fresh editor process and logs what was saved.

Run headless from the project root (the editor must not be open):
  UnrealEditor-Cmd.exe FactoryGame.uproject -run=pythonscript -script="<this file>" -unattended -nullrhi -nosplash
Output goes to Saved/Logs/FactoryGame.log, lines tagged [DDC] VERIFY.
"""
import unreal


def log(message):
    unreal.log(f"[DDC] VERIFY {message}")


cls = unreal.load_class(None, "/DimensionalDismantleCrate/DDCConfiguration.DDCConfiguration_C")
if cls is None:
    unreal.log_error("[DDC] VERIFY: class not found")
    raise RuntimeError("class not found")
cdo = unreal.get_default_object(cls)
config_id = cdo.get_editor_property("config_id")
log(f"config_id = {config_id.get_editor_property('mod_reference')!r} / {config_id.get_editor_property('config_category')!r}")
log(f"display_name = {cdo.get_editor_property('display_name')}")
root = cdo.get_editor_property("root_section")
if root is None:
    unreal.log_error("[DDC] VERIFY: root_section is None")
    raise RuntimeError("root_section missing")
log(f"root_section class = {root.get_class().get_name()} outer = {root.get_outer().get_path_name()} widget_type = {root.get_editor_property('widget_type')}")
for key, prop in root.get_editor_property("section_properties").items():
    extra = ""
    if "Integer" in prop.get_class().get_name():
        enum = prop.get_editor_property("enum_class")
        extra = "min={} max={} widget={} enum={}".format(
            prop.get_editor_property("min_value"),
            prop.get_editor_property("max_value"),
            prop.get_editor_property("widget_type"),
            enum.get_path_name() if enum else None,
        )
    log("{} -> {} default={} {} world_reload={} name='{}' tooltip_lines={}".format(
        key,
        prop.get_class().get_name(),
        prop.get_editor_property("default_value"),
        extra,
        prop.get_editor_property("requires_world_reload"),
        prop.get_editor_property("display_name"),
        len(str(prop.get_editor_property("tooltip")).splitlines()),
    ))
