// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Piano_v2 : ModuleRules
{
	public Piano_v2(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "Python3" });

		// ink wash: ProceduralMeshComponent for the brush stroke and the mountains, Slate for the closing seal
		PrivateDependencyModuleNames.AddRange(new string[] { "ProceduralMeshComponent", "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true

		// convert.py and the MIDI files are read from Content/ as loose files at runtime, so stage them outside the pak
		RuntimeDependencies.Add("$(ProjectDir)/Content/convert.py", StagedFileType.NonUFS);
		RuntimeDependencies.Add("$(ProjectDir)/Content/MIDI/*.mid", StagedFileType.NonUFS);
	}
}
