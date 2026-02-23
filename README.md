# 🤖 LervikMCP - UE5 MCP Server Plugin

LervikMCP is an (experimental) MCP server plugin for Unreal Engine 5. It allows you to interface and work with the editor in new ways!

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

## Use cases

The project aims to explore different ways to interface between UE5 and LLMs. Here are some uses cases I've found interesting so far!

### Blueprint to C++

### Benchmark console variable changes

### Debug blueprints/materials/++

### Ask about things

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

The project aims to explore different ways to interface between UE5 and LLMs. This 

The plugin enables tools to modify and delete things. There is always a risk that the LLM decides to run a delete/modify command on the wrong thing. Experiment with caution.

The `execute_python` tool allows for a script interface between the LLM and UE5. This also allows for arbitrary python code execution.

## Settings

### mcp.enable

- Default: `0`
- Options: `0` or `1`

Turns the MCP server ON or OFF

### mcp.port

- Default: `8090`
- Options: any valid port

Sets the port the MCP server should use.