using UnrealBuildTool;

public class DimensionalDismantleCrate : ModuleRules
{
	public DimensionalDismantleCrate(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// Development-only test harness (crate spawning, the automatic test run, the client crate log).
		// 0 for release builds; set 1 locally to run Tools/run_autotest.ps1 or Tools/tests/ddc_ds_test.sh.
		// The dismantle steps of the test also need Config/AccessTransformers.ini, which is deleted for release builds.
		PublicDefinitions.Add("DDC_DEV_COMMANDS=0");

		// FactoryGame transitive dependencies (same set as the SML starter template, trimmed to what is used).
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject",
			"Engine",
			"DeveloperSettings",
			"PhysicsCore",
			"InputCore",
			"GeometryCollectionEngine",
			"AnimGraphRuntime",
			"AssetRegistry",
			"NavigationSystem",
			"AIModule",
			"GameplayTasks",
			"SlateCore", "Slate", "UMG",
			"RenderCore",
			"CinematicCamera",
			"Foliage",
			"NetCore",
			"GameplayTags",
			"Json", "JsonUtilities"
		});

		// Header stubs
		PublicDependencyModuleNames.Add("DummyHeaders");

		if (Target.Type == TargetRules.TargetType.Editor) {
			PublicDependencyModuleNames.Add("AnimGraph");
			// FEnumEditorUtils for UDDCEditorTools (headless asset scripts only).
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
		PublicDependencyModuleNames.AddRange(new string[] {"FactoryGame", "SML"});
	}
}
