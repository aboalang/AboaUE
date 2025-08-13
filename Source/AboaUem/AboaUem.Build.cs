// Copyright 2023 - 2025 Christopher Augustus
//
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
using UnrealBuildTool;

public class AboaUem : ModuleRules {
  public AboaUem(ReadOnlyTargetRules Target) : base(Target) {
    DefaultBuildSettings = BuildSettingsVersion.V2;
    IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_1;
    PrivateDependencyModuleNames.AddRange(new string[] {
      "Core",
      "CoreUObject",
      "Engine",
      "UMG"
    });
    RuntimeDependencies.Add(
      PluginDirectory + "/Source/aboa/*.aboa");
    ShadowVariableWarningLevel = WarningLevel.Warning;
      // ^ !!! needed for aboa-s7.c when compiled for Android in Xcode on macOS:
    bEnableUndefinedIdentifierWarnings = false;
      // ^ note: deprecated in UE 5.5 which uses this instead:
      //UndefinedIdentifierWarningLevel = WarningLevel.Warning;
    //OptimizeCode = CodeOptimization.Never;
      // ^ !!! uncomment to debug local vars that get optimized away
  }
}
