---
name: unreal-engine-cpp
description: Unreal Engine C++ coding workflow (modules, UHT macros, Build.cs, logging, and build/run steps) for this repository.
---

# Unreal Engine C++ Skill

> **上下文导航**
> - 仓库级 Copilot 指令：[`.github/copilot-instructions.md`](../../copilot-instructions.md)
> - AI Coding 唯一权威源：[`DOC/AI_Coding_Guide.md`](../../../DOC/AI_Coding_Guide.md)
> - 相关 Skill：[Blueprint/Widget MCP](../unreal-blueprint/SKILL.md) · [Material MCP](../unreal-material/SKILL.md)

## What this skill helps with

Use this skill when working on Unreal Engine C++ in this repo, especially when you need a reliable workflow for:

- Adding or modifying C++ classes that interact with UHT (UCLASS/UPROPERTY/UFUNCTION)
- Editing module dependencies (Build.cs) and include paths
- Choosing the right build target/configuration (Editor vs Game; Development vs DebugGame)
- Diagnosing common compile/UHT issues (missing generated headers, module dependency errors)

## When to use

- You are implementing gameplay features in C++ (Actors, Components, Subsystems, Controllers, etc.)
- You need to add/refactor a UObject type with reflection
- You see build errors related to UHT, generated headers, or missing module dependencies
- You need a consistent “change → build → validate” loop

## Default workflow (step-by-step)

1) Identify the correct module
- Find the module under `Source/` and confirm its `.Build.cs`.
- Prefer making changes within an existing module unless there is a clear reason to add a new module.

2) Create or modify C++ types safely
- For reflection types:
  - `UCLASS()` / `USTRUCT()` / `UENUM()` definitions should live in headers.
  - Include the generated header last in the header file: `#include "MyType.generated.h"`.
- Prefer forward declarations in headers; include heavy headers in `.cpp`.

2.5) Verify API usage with LSP (required)
- For every Unreal API you call, use LSP hover/definition to confirm the symbol exists and the signature/params match your usage.
- Record the exact engine header file and line as evidence.
- If LSP returns no result for a symbol, immediately fall back to a text search and cite the matching engine source location.

3) Update module dependencies (Build.cs) when needed
- If you include headers from another module, add it to `PublicDependencyModuleNames` or `PrivateDependencyModuleNames` as appropriate.
- Keep dependencies minimal.

4) Use Unreal logging consistently
- Use `UE_LOG` with an existing category or define a new category for a new subsystem.
- Avoid spamming logs in tick unless behind verbosity guards.

5) Build and validate
- Prefer building an *Editor* target when iterating on editor-play testing.
- If you changed reflection macros (UHT), expect UHT to run; ensure generated headers are present.
- If build failures occur, follow the “Common failures” checklist below.

## Epic C++ coding standard (required)

Follow Epic’s C++ coding standard for all changes. Key requirements:

- Class organization: declare public first, then protected, then private.
- Naming conventions:
  - Use PascalCase for types/vars; no underscores between words.
  - Prefix types: `U` (UObject), `A` (AActor), `S` (SWidget), `I` (interface), `F` (most structs/classes), `T` (templates), `E` (enums), `C` (concept-alike structs).
  - Boolean variables must start with `b` (for example, `bIsVisible`).
  - Function names are verbs; bool-returning functions must read as a true/false question (for example, `IsReady`).
  - Out parameters should be prefixed with `Out` (and `bOut` for bool).
- Inclusive language: avoid non-inclusive terms (for example, use allow/deny list instead of whitelist/blacklist) and avoid slang/profanity.
- Types: prefer UE fixed-width types (for example, `int32`, `uint8`), `TCHAR`, `PTRINT`. Never use `BOOL`.
- Standard library: prefer UE idioms, but use the standard library where explicitly recommended; avoid mixing UE and standard idioms in the same API.
- Comments: document intent, keep comments accurate; prefer self-documenting code; update comments with behavior changes.
- Const correctness: use `const` for non-mutating parameters, methods, and locals; never use `const` on return types.
- Modern C++:
  - Use `override` and `final` when applicable.
  - Use `nullptr` (not `NULL`).
  - Avoid `auto` except for lambdas, verbose iterators, or template-dependent types.
  - Prefer range-based `for`.
  - Use `enum class` (use `uint8` for Blueprint enums) and `ENUM_CLASS_FLAGS` for flags.
  - Use explicit lambda captures; avoid implicit `[&]`/`[=]`.
- Formatting:
  - Braces on a new line; always include braces for single-statement blocks.
  - Indent with tabs (tab size 4).
  - No space between function name and `(`.
  - Pointer/reference style: `FThing* Ptr` (space before name only).
  - Always wrap string literals with `TEXT()`.
- Namespaces:
  - UHT does not support namespaces for `UCLASS/USTRUCT`.
  - Put non-U APIs in `UE::` namespaces; avoid `using` declarations in global scope.
- Physical dependencies:
  - Use `#pragma once` in headers.
  - Prefer forward declarations; include headers directly and as narrowly as possible (avoid `Core.h`).
- Encapsulation: members should be private unless required; provide protected accessors for subclasses.
- General style: no shadowed variables; avoid magic literals in calls; avoid large inline functions and excessive `FORCEINLINE`.
- API design: avoid bool parameter flags; prefer enum flags or params struct; avoid overloading by `bool`/`FString`.
- Platform-specific code: keep platform code in platform-specific folders; avoid `PLATFORM_*` in cross-platform code.

## Common failures checklist

### Missing `*.generated.h`
- Ensure the header includes `MyType.generated.h` and that it is the last include in that header.
- Ensure the file is located in a module that is part of the build target.
- Ensure the type is declared with the right `UCLASS/USTRUCT` macros and includes required UE headers.

### “Unrecognized type” / UHT parsing errors
- Check for missing `UCLASS()` / `GENERATED_BODY()`.
- Check for includes/forward declarations for reflected types.
- Avoid templates/complex macros in UPROPERTY/UFUNCTION signatures unless you know they are supported.

### Linker errors / unresolved externals
- Usually a missing module dependency or missing implementation file.
- Confirm the symbol lives in the linked module and the `.Build.cs` dependencies include it.

## Output expectations

When using this skill, produce:
- The exact file(s) and symbol(s) to change (class/function/module)
- A minimal, compile-friendly patch plan
- Build target suggestion (Editor/Game and configuration)
- A quick validation checklist
- A brief note confirming coding-standard compliance (naming, formatting, and const correctness)
- A short LSP validation summary with source references (file + line)

## Repository-specific notes

- Prefer VS Code build tasks when available.
- For larger refactors, keep changes small and build often.
- For diagnostics/log-context tasks, this repo provides an optional parallel MCP server `ue-editor-mcp-logs` with tool `unreal.logs.get` (supports `auto/live/saved` and offline `Saved/Logs` fallback when UE is not reachable).

## MVVM / UMG Viewmodel (UMG Viewmodel plugin)

> Reference: https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-viewmodel-for-unreal-engine

- Enable the plugin: ModelViewViewModel (UMG Viewmodel) is required for `UMVVMViewModelBase` and View Bindings.
- Viewmodel: Derive from `UMVVMViewModelBase` or implement `INotifyFieldValueChanged`.
- FieldNotify: `UPROPERTY(..., FieldNotify)` or `UFUNCTION(..., FieldNotify, BlueprintPure)` are required for OneWay/TwoWay bindings; C++ must broadcast manually.
- Update macros: Use `UE_MVVM_SET_PROPERTY_VALUE` or `UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED` to push UI updates.
- Binding direction: Use One Way to Widget for read-only status; use One Way to Viewmodel or Two Way for input/settings.
- Runtime assignment: Use `UMVVMView::SetViewModel` or `UMVVMBlueprintLibrary::SetViewModelByClass`.
- Scope: Keep viewmodels small and focused; use nested viewmodels for complex data.

### Recommended boundaries and minimal setup

- C++ (authoritative state + mutation entry points)
  - `UStatusComponent`: Health/Energy/Score (current values and max values) and `ApplyDamage`, `ConsumeEnergy`, `AddScore`.
  - `UStatusViewModel`: `HealthPercent`, `EnergyPercent`, `ScoreText`, updated from component delegates via `UE_MVVM_SET_PROPERTY_VALUE`.
- Keep data-driven and Blueprint-friendly
  - Initial values/max values/regen rates/thresholds: `DataAsset` / `Curve` / data tables.
  - Character presentation/animation/input/VFX: Blueprint Character.
  - UI layout/animation: Widget Blueprint.

## Engine-First Implementation

When implementing any C++ feature related to UE Editor / Blueprint / asset operations, **always look for and reuse existing engine implementations first** instead of hand-crafting behavior from scratch.

### Why

- Internal engine operations (for example, Blueprint function rename) usually require multi-step coordination (updating call-site references, refreshing nodes, marking structural changes, and more). Hand-rolled sequences often miss critical steps and cause data inconsistency or compile failures.
- Engine APIs already include broad edge-case handling and testing coverage that ad-hoc custom logic usually cannot match.

### Workflow

1. **Search engine source first**: Look under `Engine/Source/Editor/` and `Engine/Source/Runtime/` for relevant keywords (for example, `RenameGraph`, `RenameFunction`) and find the editor UI entry points that perform the same operation.
2. **Trace the call chain**: Follow the flow from UI entry points (for example, `SMyBlueprint`, `SBlueprintPalette`) down to utility APIs (for example, `FBlueprintEditorUtils::RenameGraph`) to understand the full sequence.
3. **Call engine APIs directly**: If a complete engine API exists, use it directly; do not reassemble internal steps manually.
4. **Implement custom logic only when necessary**: If no engine API exists, implement a minimal custom solution and document why reuse was not possible.

### Lessons Learned (Blueprint Function Rename)

| Approach | Result |
|------|------|
| `RenameGraphWithSuggestion()` + manual `K2Node_CallFunction` call-site updates | ❌ Failed — `MarkBlueprintAsStructurallyModified` triggers `ReconstructNode`, which invalidates nodes before/after manual reference updates |
| Manual `SetMemberName` + skipping `MarkBlueprintAsStructurallyModified` | ❌ Failed — later `MarkBlueprintAsModified` still resets references |
| **Directly calling `FBlueprintEditorUtils::RenameGraph()`** | ✅ Success — engine handles the entire process in correct order |

**Key finding**: `RenameGraphWithSuggestion()` is only a minimal `Graph->Rename()` wrapper and does not update call-site references. `RenameGraph()` is the complete rename path used by the My Blueprint panel, including call-site updates, local-variable scope fixes, and structural modification handling.

### Common Engine Utility APIs

- `FBlueprintEditorUtils::RenameGraph()` — Complete Blueprint function/macro graph rename with call-site updates
- `FBlueprintEditorUtils::RemoveGraph()` — Safe graph removal with reference cleanup
- `FBlueprintEditorUtils::AddFunctionGraph()` — Adds a function graph to a Blueprint
- `FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified()` — Marks structural Blueprint changes and triggers full node refresh
- `FKismetEditorUtilities::CompileBlueprint()` — Compiles a Blueprint

---

## Examples and resources

### Core Examples (Required)

- [Adding a reflected UObject type](./examples/add-reflected-uobject.md) - Complete example for a reflected UObject-derived class
- [Simple AActor-derived class](./examples/simple-actor-example.md) - Basic Actor implementation pattern
- [USTRUCT and UENUM](./examples/ustruct-and-uenum.md) - Reflection patterns for structs and enums
- [Delegates (Event System)](./examples/delegates-events.md) - Delegate and event-system usage
- [Network Replication](./examples/network-replication.md) - Property replication and RPC patterns
- [Logging and Debug](./examples/logging-and-debug.md) - Logging system and debugging practices
- [MVVM Status HUD](./examples/mvvm-status-hud.md) - Minimal status HUD with UMG Viewmodel

### LSP-validated source references (engine headers)

- CreateDefaultSubobject: [UE_5.7/Engine/Source/Runtime/CoreUObject/Public/UObject/Object.h](UE_5.7/Engine/Source/Runtime/CoreUObject/Public/UObject/Object.h#L125)
- GetLifetimeReplicatedProps: [UE_5.7/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h](UE_5.7/Engine/Source/Runtime/Engine/Classes/GameFramework/Actor.h#L273)
- DOREPLIFETIME: [UE_5.7/Engine/Source/Runtime/Engine/Public/Net/UnrealNetwork.h](UE_5.7/Engine/Source/Runtime/Engine/Public/Net/UnrealNetwork.h#L259)
- DECLARE_DYNAMIC_MULTICAST_DELEGATE: [UE_5.7/Engine/Source/Runtime/Core/Public/Delegates/DelegateCombinations.h](UE_5.7/Engine/Source/Runtime/Core/Public/Delegates/DelegateCombinations.h#L38)
- UE_LOG: [UE_5.7/Engine/Source/Runtime/Core/Public/Logging/LogMacros.h](UE_5.7/Engine/Source/Runtime/Core/Public/Logging/LogMacros.h#L270)
- UMVVMViewModelBase + FieldNotify macros: [UE_5.7/Engine/Plugins/Runtime/ModelViewViewModel/Source/ModelViewViewModel/Public/MVVMViewModelBase.h](UE_5.7/Engine/Plugins/Runtime/ModelViewViewModel/Source/ModelViewViewModel/Public/MVVMViewModelBase.h#L15-L42)
- MVVM View SetViewModel: [UE_5.7/Engine/Plugins/Runtime/ModelViewViewModel/Source/ModelViewViewModel/Public/View/MVVMView.h](UE_5.7/Engine/Plugins/Runtime/ModelViewViewModel/Source/ModelViewViewModel/Public/View/MVVMView.h#L152-L170)
- MVVM Blueprint SetViewModelByClass: [UE_5.7/Engine/Plugins/Runtime/ModelViewViewModel/Source/ModelViewViewModel/Public/MVVMBlueprintLibrary.h](UE_5.7/Engine/Plugins/Runtime/ModelViewViewModel/Source/ModelViewViewModel/Public/MVVMBlueprintLibrary.h#L14-L15)

### CoreMinimal.h Common Includes

When you include `CoreMinimal.h`, you automatically get access to these essential UE headers:

- **Core Types**: `CoreTypes.h`, `CoreFwd.h`
- **Containers**: `Containers/Array.h`, `Containers/UnrealString.h`, `Containers/Map.h`, `Containers/Set.h`
- **Math**: `Math/Vector.h`, `Math/Rotator.h`, `Math/Transform.h`, `Math/UnrealMathUtility.h`
- **UObject System**: `UObject/NameTypes.h`, `UObject/UnrealNames.h`
- **Templates**: `Templates/SharedPointer.h`, `Templates/UniquePtr.h`, `Templates/TypeHash.h`
- **Logging**: `Logging/LogMacros.h`, `Logging/LogCategory.h`
- **Delegates**: `Delegates/Delegate.h`, `Delegates/MulticastDelegateBase.h`

### Additional Common Headers (not in CoreMinimal)

For specific functionality, you may need these additional includes:

```cpp
// For gameplay framework classes
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameStateBase.h"

// For components
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SphereComponent.h"
#include "Components/BoxComponent.h"

// For UI (requires UMG module)
#include "Blueprint/UserWidget.h"
#include "Components/WidgetComponent.h"

// For Enhanced Input (UE5)
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"

// For AI (requires AIModule)
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"

// For networking
#include "Net/UnrealNetwork.h"

// For timers
#include "TimerManager.h"

// For assets
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
```

### Common Module Dependencies (Build.cs)

```csharp
// Minimal gameplay project
PublicDependencyModuleNames.AddRange(new string[] { 
    "Core", 
    "CoreUObject", 
    "Engine", 
    "InputCore"
});

// With Enhanced Input (UE5)
PublicDependencyModuleNames.Add("EnhancedInput");

// With UI
PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "UMG" });

// With AI
PublicDependencyModuleNames.Add("AIModule");
PublicDependencyModuleNames.Add("NavigationSystem");

// With Gameplay Abilities
PublicDependencyModuleNames.Add("GameplayAbilities");
PublicDependencyModuleNames.Add("GameplayTags");
PublicDependencyModuleNames.Add("GameplayTasks");

// With Physics
PublicDependencyModuleNames.Add("PhysicsCore");
PublicDependencyModuleNames.Add("Chaos");

// With Niagara particles
PublicDependencyModuleNames.Add("Niagara");

// With networking
PublicDependencyModuleNames.Add("NetCore");
PublicDependencyModuleNames.Add("OnlineSubsystem");
```

### UE5 Specific Notes

- **TObjectPtr**: In UE5, prefer `TObjectPtr<T>` over raw `T*` for `UPROPERTY` object references.
- **Enhanced Input**: In UE5, prefer the Enhanced Input system over the legacy input system.
- **Soft/Weak References**: Use `TSoftObjectPtr` for deferred asset loading and `TWeakObjectPtr` to avoid preventing garbage collection.
- **World Partition**: Use World Partition for large open-world maps.
- **Nanite/Lumen**: Ensure rendering-related code is compatible with Nanite and Lumen.

---

## Memory MCP — 任务完成前必须执行的检查清单

> 权威源：`.github/copilot-instructions.md` §2 | 详细说明：`MCP/memory/README.md`

⚠️ **本清单为阻塞性要求 — C++ 任务完成后、向用户报告结果前，必须逐项执行：**

- [ ] `memory_guard_check()` — 确认容量未超限
- [ ] 功能实现完成 → `memory_write("memory-bank/progress.md", ...)` 追加变更记录
- [ ] C++ 架构/模块依赖变更 → `memory_write("memory-bank/techContext.md", ...)`
- [ ] 新编码模式/约定 → `memory_write("memory-bank/systemPatterns.md", ...)`
- [ ] 编译/UHT 错误 → `memory_write(".ai-context/latest-error.md", ...)`
- [ ] `memory_write("memory-bank/activeContext.md", ...)` — 写入会话摘要（每次必做）

不适用的项跳过，但 **`progress.md` 和 `activeContext.md` 在有实际改动时不得省略**。
