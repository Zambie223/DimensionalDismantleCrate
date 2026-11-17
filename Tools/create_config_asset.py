"""Creates or updates /DimensionalDismantleCrate/DDCConfiguration, the mod's SML Mod Configuration asset.

Run headless from the project root (the editor must not be open, and the editor build must be current):
  UnrealEditor-Cmd.exe FactoryGame.uproject -run=pythonscript -script="<this file>" -unattended -nullrhi -nosplash
Output goes to Saved/Logs/FactoryGame.log, lines tagged [DDC].

The asset is a Blueprint because SML's Mod Options widgets come from its Blueprint property classes.
Keys must match DDCConstants.h; RateMode values must match EDDCRateMode in DDCRateMode.h.
"""
import unreal

MOD = "DimensionalDismantleCrate"
ASSET_DIR = f"/{MOD}"
ASSET_NAME = "DDCConfiguration"
ASSET_PATH = f"{ASSET_DIR}/{ASSET_NAME}"
SML_PROPS = "/SML/Interface/UI/Menu/Mods/ConfigProperties"
# UserDefinedEnum asset for the dropdown labels: a C++ UENUM's DisplayName metadata is stripped from Shipping builds,
# so the game showed "PerCrate". Order must match DDC::ERateMode in DDCConstants.h (the config stores the index).
RATE_MODE_ENUM_NAME = "DDCRateModeEnum"
RATE_MODE_ENUM_PATH = f"{ASSET_DIR}/{RATE_MODE_ENUM_NAME}"
RATE_MODE_LABELS = ["Per crate", "Shared", "Per stack slot"]

HOST_NOTE = "In multiplayer only the host's or server's setting is used."

PROPERTIES = [
    {
        "key": "Enabled",
        "type": "bool",
        "display_name": "Upload crates to the Dimensional Depot",
        "tooltip": (
            "Dismantle crates send their items to the Dimensional Depot by themselves, at the speed of an uploader,\n"
            "whenever the depot has room. Needs the Dimensional Depot research and at least one uploader built.\n" + HOST_NOTE
        ),
        "default": True,
    },
    {
        "key": "RateMode",
        "type": "enum",
        "display_name": "Upload speed",
        "tooltip": (
            "Per crate: every crate uploads like one Dimensional Depot Uploader.\n"
            "Shared: all crates together upload like one uploader, taking turns.\n"
            "Per stack slot: every filled slot in a crate uploads like one uploader (much faster for big crates).\n"
            "Upload speed research and other mods that change the depot's upload speed apply in every mode.\n" + HOST_NOTE
        ),
        "default": 0,
        "min": 0,
        "max": 2,
    },
    {
        "key": "IncludeDeathCrates",
        "type": "bool",
        "display_name": "Also upload death crates",
        "tooltip": (
            "Also upload the crates players drop when they die. Off by default, so your belongings stay where you died.\n"
            + HOST_NOTE
        ),
        "default": False,
    },
    {
        "key": "UploadDeathCrateEquipment",
        "type": "bool",
        "display_name": "Upload equipment from death crates",
        "tooltip": (
            "Only matters when death crates are uploaded. Off: equipment, and items that store something (fuel, ammo, ...),\n"
            "stay in the death crate. On: they are uploaded too.\n"
            "Risk: the Dimensional Depot only stores item counts, so anything an item stores (for example remaining fuel or\n"
            "loaded ammo) is lost when it is uploaded.\n" + HOST_NOTE
        ),
        "default": False,
    },
    {
        "key": "SendDismantleOverflowToDepot",
        "type": "bool",
        "display_name": "Send dismantle overflow straight to the depot",
        "tooltip": (
            "When you dismantle something and your inventory is full, the items that don't fit go straight into the\n"
            "Dimensional Depot instead of a dismantle crate, without the upload wait, as long as the depot has room.\n"
            "Items the depot can't take still go into a crate. In multiplayer, very large amounts arrive over a few seconds.\n"
            "Needs the Dimensional Depot research and at least one uploader built. Works whether or not crates upload.\n"
            + HOST_NOTE
        ),
        "default": False,
    },
]


def fail(message):
    unreal.log_error(f"[DDC] {message}")
    raise RuntimeError(message)


def load_sml_class(name):
    cls = unreal.load_class(None, f"{SML_PROPS}/{name}.{name}_C")
    if cls is None:
        fail(f"Could not load SML class {name}")
    return cls


def generated_class_of(blueprint):
    try:
        cls = unreal.BlueprintEditorLibrary.generated_class(blueprint)
        if cls is not None:
            return cls
    except Exception as error:  # library may be unavailable; fall back to loading by path
        unreal.log_warning(f"[DDC] BlueprintEditorLibrary.generated_class failed: {error}")
    return unreal.load_class(None, f"{ASSET_PATH}.{ASSET_NAME}_C")


def get_or_create_blueprint():
    """Updates the existing asset in place, so the asset never has to be deleted.

    Tries loading first: in the headless commandlet the asset registry has not scanned the plugin, so
    does_asset_exist() says False for an asset that exists, and create_asset() then collides with it.
    """
    blueprint = unreal.load_asset(ASSET_PATH)
    if blueprint is not None:
        return blueprint
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", unreal.ModConfiguration)
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(ASSET_NAME, ASSET_DIR, unreal.Blueprint, factory)
    if blueprint is None:
        fail("create_asset returned None")
    return blueprint


def get_or_create_subobject(existing, cls, outer, name):
    """Reuses an existing instanced subobject of the right class, so re-running never clashes on the object name."""
    if existing is not None and existing.get_class() == cls:
        return existing
    return unreal.new_object(cls, outer=outer, name=name)


def get_or_create_rate_enum():
    enum = unreal.load_asset(RATE_MODE_ENUM_PATH)
    if enum is None:
        enum = unreal.AssetToolsHelpers.get_asset_tools().create_asset(RATE_MODE_ENUM_NAME, ASSET_DIR, unreal.UserDefinedEnum, unreal.EnumFactory())
    if enum is None:
        fail("Could not create the rate mode enum asset")
    if not unreal.DDCEditorTools.setup_user_defined_enum(enum, [unreal.Text(label) for label in RATE_MODE_LABELS]):
        fail("SetupUserDefinedEnum failed; see LogDimensionalDismantleCrate")
    if not unreal.EditorAssetLibrary.save_loaded_asset(enum, False):
        fail("Saving the rate mode enum failed")
    unreal.log(f"[DDC] Saved {RATE_MODE_ENUM_PATH} with {RATE_MODE_LABELS}")
    return enum


def main():
    rate_enum = get_or_create_rate_enum()

    blueprint = get_or_create_blueprint()
    cls = generated_class_of(blueprint)
    if cls is None:
        fail("No generated class for the Blueprint")
    cdo = unreal.get_default_object(cls)

    section_cls = load_sml_class("BP_ConfigPropertySection")
    property_classes = {
        "bool": load_sml_class("BP_ConfigPropertyBool"),
        "enum": load_sml_class("BP_ConfigPropertyInteger"),
    }

    cdo.modify()
    root = get_or_create_subobject(cdo.get_editor_property("root_section"), section_cls, cdo, "RootSection")
    root.modify()
    existing_properties = root.get_editor_property("section_properties") or {}

    section_properties = {}
    for spec in PROPERTIES:
        kind = spec["type"]
        prop = get_or_create_subobject(existing_properties.get(spec["key"]), property_classes[kind], root, spec["key"])
        prop.modify()
        prop.set_editor_property("display_name", spec["display_name"])
        prop.set_editor_property("tooltip", spec["tooltip"])
        prop.set_editor_property("default_value", spec["default"])
        prop.set_editor_property("value", spec["default"])
        if kind == "enum":
            prop.set_editor_property("widget_type", unreal.CP_IntegerWidgetType.CPI_ENUM)
            prop.set_editor_property("enum_class", rate_enum)
            prop.set_editor_property("min_value", spec["min"])
            prop.set_editor_property("max_value", spec["max"])
        prop.set_editor_property("requires_world_reload", False)
        section_properties[spec["key"]] = prop
    root.set_editor_property("section_properties", section_properties)
    # SML defaults sections to a horizontal list (with a scroll bar); the Mods menu reads top to bottom, so stack them.
    root.set_editor_property("widget_type", unreal.CP_SectionWidgetType.CPS_VERTICAL)

    config_id = unreal.ConfigId()
    config_id.set_editor_property("mod_reference", MOD)
    config_id.set_editor_property("config_category", "")
    cdo.set_editor_property("config_id", config_id)
    cdo.set_editor_property("display_name", "Dimensional Dismantle Crate")
    cdo.set_editor_property("description", "Dismantle crates upload their items to the Dimensional Depot at uploader speed.")
    cdo.set_editor_property("root_section", root)

    if not unreal.EditorAssetLibrary.save_loaded_asset(blueprint, False):
        fail("Saving the asset failed")
    unreal.log(f"[DDC] Saved {ASSET_PATH}")


main()
