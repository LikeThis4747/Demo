// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Demo : ModuleRules
{
	public Demo(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
	
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "AIModule", "NavigationSystem", "GameplayTasks", "UMG" });

		// UMG 委托签名使用 ETextCommit 等 SlateCore 反射类型。
		PrivateDependencyModuleNames.AddRange(new string[] { "PhysicsControl", "Slate", "SlateCore", "GeometryCollectionEngine" });

		// 使用 Slate UI 时取消下一行注释。
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });
		
		// 使用在线功能时取消下一行注释。
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// 如需接入 OnlineSubsystemSteam，请在 uproject 的插件列表中添加该插件并将 Enabled 设为 true。
	}
}
