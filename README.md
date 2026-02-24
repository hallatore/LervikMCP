# LervikMCP - UE5 MCP Plugin

<p align="center">
  <a href="https://discord.gg/sX48CssHWM"><img src="https://img.shields.io/discord/1279047221362294964?label=Discord&logo=discord&logoColor=white&color=5865F2&style=for-the-badge" alt="Discord"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-blue.svg?style=for-the-badge" alt="MIT License"></a>
</p>

LervikMCP is an MCP plugin for Unreal Engine 5. It allows you to interface and work with the editor in new ways! It's an experimental project exploring how LLMs can interact with the UE5 Editor.

## Examples - What can it do?

The project aims to explore different ways to interface between UE5 and LLMs. Here are some examples I've found interesting so far!

```
What does this blueprint do?
```
![alt text](public/blueprint.png)
---

```
List the modified settings of the lights I've selected
```
![alt text](public/selected_lights.png)
---

```
Do a trace and give me a breakdown of the frametime (depth 2)
```
![alt text](public/trace.png)
---

## Tools (So far)

**`find`** — Search for assets, actors, classes, or properties in the editor with name/path/class filters. Also retrieves the current editor selection.

**`inspect`** — Inspect detailed info on an asset or actor: properties, components, Blueprint nodes/variables/functions/pins, material expressions, or connections.

**`create`** — Spawn a new actor in the level or create a new asset (Blueprint, Material, MaterialInstance), with optional duplication from a template.

**`delete`** — Delete assets, actors, Blueprint nodes/variables/components, material expressions, or break pin connections.

**`modify`** — Set properties and/or transform (location, rotation, scale) on any actor or object in the level.

**`graph`** — Edit Blueprint and Material node graphs: add/edit/connect nodes, manage variables, functions, and components.

**`editor`** — Manage editor state: open/close/save assets, select/deselect/focus level actors, or navigate the Content Browser to a path.

**`get_open_assets`** — Returns the name, path, and type of all currently open assets in the editor.

**`execute`** — Get, set, or list console variables (CVars) in the editor.

**`execute_python`** — Run arbitrary Python code in the editor via the Unreal Python API, wrapped in an undo transaction.

**`trace`** — Control Unreal Insights tracing (start/stop/status) and analyze GPU profiling data from `.utrace` files.

## Getting Started

### Setup against launcher version

```powershell
.\scripts\build-plugin.ps1 -version 5.7
.\scripts\deploy-plugin.ps1 -version 5.7
```

### Setup against custom build

```powershell
.\scripts\build-plugin.ps1 -enginePath "<Path to engine>"
.\scripts\deploy-plugin.ps1 -enginePath "<Path to engine>"
```

### In editor
1. Enable LervikMCP plugin & Restart editor
2. Enable mcp server from toolbar, or set `mcp.enable 1`
3. Add http://localhost:8090/mcp as an MCP server in your LLM setup (Claude, Copilot, Etc)

Note: The server is always off by default. It only runs on localhost, but there is no authentication against it.

### ⚠️Experimental warning

The project aims to explore different ways to interface between UE5 and LLMs.

The plugin enables tools to modify and delete things. There is always a risk that the LLM decides to run a delete/modify command on the wrong thing. Experiment with caution.

The `execute_python` tool allows for a script interface between the LLM and UE5. It has extra validation (`mcp.python.hardening`) to only execute safe code, but there is never a guarantee.

## Settings

### mcp.enable

- Default: `0`
- Options: `0` or `1`

Turns the MCP server ON or OFF

### mcp.port

- Default: `8090`
- Options: any valid port

Sets the port the MCP server should use.

### mcp.python.hardening

- Default: `2`
- Options:
    - `0` - None
    - `1` - Medium
    - `2` - High

Safety measure that attempts to sandbox the `execute_python` tool.