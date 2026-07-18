---
name: unreal-python
description: >
  Unified guide for UE5 Editor Python scripting: script development workflow with visual verification,
  API search from unreal.py stubs, and VSCode F5 debugging via debugpy. Use when writing UE5 Python
  scripts, querying UE5 Python API, or setting up VSCode debugging for UE5 Python.
---

# UE5 Python Development Skill

Unified skill for UE5 Editor Python scripting: development workflow, API search, and VSCode debugging.

## Skill Structure

```
unreal-python/
├── SKILL.md              # This file
├── lib/                  # Shared Python libraries
│   ├── unreal_python_utils/  # Project path utilities
│   └── ue5_remote/           # UE5 remote execution protocol
├── scripts/              # CLI tools
│   ├── api-search.py         # UE5 API fuzzy/exact search
│   ├── remote-execute.py     # Send Python to UE5 editor
│   ├── setup-vscode.py       # Generate .vscode configs
│   ├── start_debug_server.py # Start debugpy inside UE5
│   ├── asset-diagnostic.py   # Asset diagnostics
│   ├── orbital-capture.py    # Scene capture
│   ├── window-capture.py     # Window/asset capture
│   └── pie-capture.py        # PIE capture
├── assets/               # Template files
│   ├── vscode-launch-template.json
│   └── vscode-tasks-template.json
├── examples/             # Example scripts
├── references/           # Detailed reference docs
├── site-packages/        # Bundled Python modules
└── plugin/               # ExtraPythonAPIs UE plugin
```

---

## Part 1: Script Development Workflow

### Prerequisites

CLI scripts for capture and diagnostics are in `scripts/`. See [Capture Scripts Reference](./references/capture-scripts.md).

### Phase 1: Requirements

Clarify requirements until you have 95% confidence in understanding user intentions.

### Phase 2: Planning

**Do not implement yet - just plan.**

Every step implements and tests ONE script. Three types allowed:

| Type | Purpose | Testing Steps |
|------|---------|---------------|
| **Scene-Setup** | Static scene (atmosphere, lighting, actors) | diagnostic → orbital capture → visual analysis |
| **Configuration** | Properties, tags, physics, collisions | diagnostic → window capture → visual analysis |
| **Integration** | PIE mode testing | PIE capture → visual analysis |

#### Testing Protocol

Each script step has three substeps:
- **x.1** — Run `asset-diagnostic.py` to check for issues
- **x.2** — Capture screenshots (orbital/window/PIE based on type)
- **x.3** — Analyze screenshots for visual correctness

### Phase 3: Implementation

1. Check [Common Pitfalls](./references/common-pitfals/) for related documents
2. Follow [Best Practices](./references/best-practices.md)
3. Reference example scripts:
   - [Add gameplay tag](./examples/add_gameplaytag_to_asset.py)
   - [Create blendspace](./examples/create_footwork_blendspace.py)
   - [Create level](./examples/create_sky_level.py)
   - [Customize atmosphere](./examples/create_dark_pyramid_level.py)
   - [Create blueprint with physics](./examples/create_punching_bag_blueprint.py)
   - [PIE screenshot capturer](./examples/pie_screenshot_capturer.py)
4. Use UEEditorMCP `editor.execute` to run scripts in editor

### Transaction Rule

Scripts modifying assets (`set_editor_property`, `save`, `modify`) **must** wrap in transaction:
```python
with unreal.ScopedEditorTransaction("Description"):
    asset.set_editor_property(...)
```

### Visual Verification

| Scenario | Script | Key Parameters |
|----------|--------|----------------|
| Static scene | `orbital-capture.py` | `preset`, `distance`, `target_x/y/z` |
| Blueprint/Asset | `window-capture.py` | `command`, `asset_path`, `tab` |
| PIE runtime | `pie-capture.py` | `command`, `interval`, `multi_angle` |

**Presets**: `orthographic` (6 views), `perspective` (4 views), `birdseye` (4 views), `all` (14 views)

---

## Part 2: API Search

Query UE5 Python API definitions from `unreal.py` stub files. See [API Search Reference](./references/api-search.md) for full docs.

### Quick Usage

```bash
# Fuzzy search
python scripts/api-search.py actor
python scripts/api-search.py inputmapping context

# Exact class/member query
python scripts/api-search.py unreal.InputMappingContext
python scripts/api-search.py unreal.Actor.on_destroyed

# Wildcard member search
python scripts/api-search.py unreal.Actor.*location*

# Filter by type
python scripts/api-search.py -c actor    # classes only
python scripts/api-search.py -m location # methods only
python scripts/api-search.py -e collision # enum values only
```

**Requires**: ripgrep (`rg`) — `scoop install ripgrep` (Windows) or `brew install ripgrep` (macOS).

Auto-detects stub from `Intermediate/PythonStub/unreal.py`. Override with `--input`.

---

## Part 3: VSCode Debugging

Setup VSCode F5 debugging for UE5 Python via debugpy. See [VSCode Debugger Reference](./references/vscode-debugger.md) for full docs.

### Quick Setup

```bash
python scripts/setup-vscode.py                    # auto-detect project
python scripts/setup-vscode.py --project /path     # explicit project
python scripts/setup-vscode.py --force             # overwrite existing
```

### Debug Workflow

1. Run `setup-vscode.py` to generate `.vscode/launch.json` + `tasks.json`
2. Open Python file → Set breakpoints → Press F5
3. Select **"UE5 Python: Debug Current File"**
4. Debugger starts debugpy (port 19678), sends file to UE5, attaches

### Attach-Only Mode

```bash
python scripts/remote-execute.py --file scripts/start_debug_server.py
# Then F5 → "UE5 Python: Attach Only"
```

### Remote Execution

```bash
python scripts/remote-execute.py --file myscript.py
python scripts/remote-execute.py --code "print('Hello from UE5')"
python scripts/remote-execute.py --file myscript.py --detached --wait 1
```

---

## References

- [Best Practices](./references/best-practices.md) — Transactions, subsystems, debug visualization
- [Common Pitfalls](./references/common-pitfals/) — Known issues and workarounds
- [Capture Scripts](./references/capture-scripts.md) — CLI parameter docs for capture scripts
- [Editor Capture API](./references/editor-capture.md) — Python module API
- [Asset Diagnostic](./references/asset-diagnostic.md) — Diagnostic module API
- [ExtraPythonAPIs Plugin](./references/extra-python-apis.md) — Socket/bone attachment
- [C++ Source Investigation](./references/cpp-source-investigation.md) — When Python API is insufficient
- [API Search](./references/api-search.md) — Fuzzy/exact API search tool
- [VSCode Debugger](./references/vscode-debugger.md) — F5 debugging setup
