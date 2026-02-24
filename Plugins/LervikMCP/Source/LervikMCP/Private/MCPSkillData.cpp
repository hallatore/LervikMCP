#include "MCPToolHelp.h"

namespace MCPToolHelp
{

// ============================================================================
// Skill: materials
// ============================================================================

static const FMCPSkillStep MaterialsSteps[] =
{
	{
		TEXT("Create the material asset"),
		TEXT("{\"tool\":\"create\",\"params\":{\"type\":\"asset\",\"name\":\"M_MyMaterial\",\"path\":\"/Game/Materials\",\"asset_type\":\"Material\"}}")
	},
	{
		TEXT("Add expression nodes (use batch for multiple). Response returns guid — save for connections."),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"add_node\",\"target\":\"/Game/Materials/M_MyMaterial\",\"node_class\":\"ScalarParameter\",\"pos_x\":-400,\"pos_y\":0}}")
	},
	{
		TEXT("Edit expression properties (ParameterName, DefaultValue, Constant, etc.)"),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"edit_node\",\"target\":\"/Game/Materials/M_MyMaterial\",\"node\":\"<GUID>\",\"properties\":{\"ParameterName\":\"Roughness\",\"DefaultValue\":0.5}}}")
	},
	{
		TEXT("Connect expressions to each other. Pin \"\" = first output. Named inputs: A, B, Coordinates."),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"connect\",\"target\":\"/Game/Materials/M_MyMaterial\",\"source\":{\"node\":\"<GUID_A>\",\"pin\":\"\"},\"dest\":{\"node\":\"<GUID_B>\",\"pin\":\"A\"}}}")
	},
	{
		TEXT("Connect expression to material root input. Valid: BaseColor, Metallic, Specular, Roughness, Normal, EmissiveColor, Opacity, OpacityMask, WorldPositionOffset, AmbientOcclusion"),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"connect\",\"target\":\"/Game/Materials/M_MyMaterial\",\"source\":{\"node\":\"<GUID>\",\"pin\":\"\"},\"dest\":{\"property\":\"BaseColor\"}}}")
	},
	{
		TEXT("Batch connections — use connections array"),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"connect\",\"target\":\"/Game/Materials/M_MyMaterial\",\"connections\":[{\"source\":{\"node\":\"<GUID_Multiply>\",\"pin\":\"\"},\"dest\":{\"property\":\"BaseColor\"}},{\"source\":{\"node\":\"<GUID_Scalar>\",\"pin\":\"\"},\"dest\":{\"node\":\"<GUID_Multiply>\",\"pin\":\"A\"}}]}}")
	},
	{
		TEXT("Inspect what you built — use type=expressions or type=connections"),
		TEXT("{\"tool\":\"inspect\",\"params\":{\"target\":\"/Game/Materials/M_MyMaterial\",\"type\":\"expressions\"}}")
	},
	{
		TEXT("Compile the material"),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"compile\",\"target\":\"/Game/Materials/M_MyMaterial\"}}")
	},
};

static const TCHAR* MaterialsTips =
	TEXT("dest uses {\"property\":\"BaseColor\"} for material root pins, {\"node\":\"GUID\",\"pin\":\"A\"} for expression-to-expression\n")
	TEXT("pin:\"\" means first/default output — works for most expressions\n")
	TEXT("Expression node_class values: Multiply, Add, Lerp, ScalarParameter, VectorParameter, TextureCoordinate, Constant, Constant3Vector, TextureSample, Clamp, OneMinus, Power, Fresnel\n")
	TEXT("pos_x and pos_y are separate integer params — not an array or object\n")
	TEXT("Use inspect type=expressions to get all node GUIDs and positions\n")
	TEXT("Edit ScalarParameter/VectorParameter properties: ParameterName, DefaultValue\n")
	TEXT("For Constant3Vector: set property Constant with value like \"(R=1.0,G=0.0,B=0.0,A=1.0)\"\n")
	TEXT("Always compile after making changes");

// ============================================================================
// Skill: blueprints
// ============================================================================

static const FMCPSkillStep BlueprintsSteps[] =
{
	{
		TEXT("Create a Blueprint asset"),
		TEXT("{\"tool\":\"create\",\"params\":{\"type\":\"asset\",\"name\":\"BP_MyActor\",\"path\":\"/Game/Blueprints\",\"asset_type\":\"Blueprint\",\"parent_class\":\"Actor\"}}")
	},
	{
		TEXT("Add variables (single or batch with variables array)"),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"add_variable\",\"target\":\"/Game/Blueprints/BP_MyActor\",\"name\":\"Health\",\"var_type\":\"float\",\"default_value\":\"100.0\"}}")
	},
	{
		TEXT("Add a function graph with inputs/outputs"),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"add_function\",\"target\":\"/Game/Blueprints/BP_MyActor\",\"name\":\"CalculateDamage\",\"inputs\":[{\"name\":\"BaseDamage\",\"type\":\"float\"}],\"outputs\":[{\"name\":\"FinalDamage\",\"type\":\"float\"}]}}")
	},
	{
		TEXT("Add nodes to EventGraph (events, function calls)"),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"add_node\",\"target\":\"/Game/Blueprints/BP_MyActor\",\"node_class\":\"CallFunction\",\"function\":\"PrintString\",\"function_owner\":\"KismetSystemLibrary\",\"pos_x\":300,\"pos_y\":0}}")
	},
	{
		TEXT("Add nodes to a function graph — use graph param to target it"),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"add_node\",\"target\":\"/Game/Blueprints/BP_MyActor\",\"graph\":\"CalculateDamage\",\"node_class\":\"CallFunction\",\"function\":\"Multiply_FloatFloat\",\"function_owner\":\"KismetMathLibrary\",\"pos_x\":200,\"pos_y\":0}}")
	},
	{
		TEXT("Add variable getter/setter nodes"),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"add_node\",\"target\":\"/Game/Blueprints/BP_MyActor\",\"node_class\":\"VariableGet\",\"variable_name\":\"Health\",\"pos_x\":0,\"pos_y\":200}}")
	},
	{
		TEXT("Connect pins — execution: source pin=then, dest pin=execute. Data: use actual pin names."),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"connect\",\"target\":\"/Game/Blueprints/BP_MyActor\",\"source\":{\"node\":\"<GUID_A>\",\"pin\":\"then\"},\"dest\":{\"node\":\"<GUID_B>\",\"pin\":\"execute\"}}}")
	},
	{
		TEXT("Discover pin names with inspect type=pins"),
		TEXT("{\"tool\":\"inspect\",\"params\":{\"target\":\"/Game/Blueprints/BP_MyActor::A1B2C3D4\",\"type\":\"pins\"}}")
	},
	{
		TEXT("Add components to the Blueprint"),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"add_component\",\"target\":\"/Game/Blueprints/BP_MyActor\",\"component_class\":\"StaticMeshComponent\",\"name\":\"MyMesh\"}}")
	},
	{
		TEXT("Compile the Blueprint"),
		TEXT("{\"tool\":\"graph\",\"params\":{\"action\":\"compile\",\"target\":\"/Game/Blueprints/BP_MyActor\"}}")
	},
};

static const TCHAR* BlueprintsTips =
	TEXT("graph (alias: graph_name) targets a specific function graph. Default: EventGraph\n")
	TEXT("Discover pin names with inspect type=pins, target=\"AssetPath::NodeGUID\"\n")
	TEXT("pos_x and pos_y are separate integer params — not an array or object\n")
	TEXT("Common node_class values: CallFunction, Event, CustomEvent, VariableGet, VariableSet, Branch, Sequence, Self, DynamicCast, SpawnActor, ForEachLoop, MacroInstance\n")
	TEXT("Execution pins: source pin=\"then\", dest pin=\"execute\"\n")
	TEXT("For CallFunction: provide function (name) and function_owner (class without U prefix)\n")
	TEXT("For Event: provide event_name (e.g. ReceiveBeginPlay, ReceiveTick, ReceiveActorBeginOverlap)\n")
	TEXT("var_type values: float, int, bool, string, byte, name, text, Vector, Rotator, Transform, Object:ClassName\n")
	TEXT("Use inspect type=nodes to see all nodes with GUIDs and positions\n")
	TEXT("Use inspect type=variables to see all variables\n")
	TEXT("Use inspect type=functions to see all function graphs\n")
	TEXT("Batch: use nodes array for add_node, connections array for connect, variables array for add_variable\n")
	TEXT("Always compile after changes");

// ============================================================================
// Skill: profiling
// ============================================================================

static const FMCPSkillStep ProfilingSteps[] =
{
	{
		TEXT("Quick capture — 5-second auto test, returns trace path and basic stats"),
		TEXT("{\"tool\":\"trace\",\"params\":{\"action\":\"test\"}}")
	},
	{
		TEXT("Manual capture for longer sessions — start, do activity, stop"),
		TEXT("{\"tool\":\"trace\",\"params\":{\"action\":\"start\",\"channels\":\"gpu,frame\"}}")
	},
	{
		TEXT("Stop manual capture"),
		TEXT("{\"tool\":\"trace\",\"params\":{\"action\":\"stop\"}}")
	},
	{
		TEXT("Analyze GPU passes — top-level overview (depth=1)"),
		TEXT("{\"tool\":\"trace\",\"params\":{\"action\":\"analyze\",\"path\":\"<trace_path>\",\"depth\":\"1\"}}")
	},
	{
		TEXT("Drill deeper into GPU tree (depth=3, filter small passes with min_ms)"),
		TEXT("{\"tool\":\"trace\",\"params\":{\"action\":\"analyze\",\"path\":\"<trace_path>\",\"depth\":\"3\",\"min_ms\":\"0.5\"}}")
	},
	{
		TEXT("Filter specific GPU passes by name (case-insensitive substring)"),
		TEXT("{\"tool\":\"trace\",\"params\":{\"action\":\"analyze\",\"path\":\"<trace_path>\",\"filter\":\"Shadow\"}}")
	},
	{
		TEXT("A/B test: capture baseline, change CVar, capture again, compare"),
		TEXT("{\"tool\":\"execute\",\"params\":{\"action\":\"set_cvar\",\"name\":\"r.Shadow.MaxResolution\",\"value\":\"512\"}}")
	},
	{
		TEXT("Check trace status"),
		TEXT("{\"tool\":\"trace\",\"params\":{\"action\":\"status\"}}")
	},
};

static const TCHAR* ProfilingTips =
	TEXT("Use action=test for quick 5-second captures — perfect for A/B comparisons\n")
	TEXT("depth controls GPU pass tree levels: 1=top-level only, 2-3=detailed breakdown\n")
	TEXT("min_ms filters out passes below a threshold (default 0.1) — use 0.5+ to focus on expensive passes\n")
	TEXT("filter is case-insensitive substring match — overrides depth limit, shows full subtree for matches\n")
	TEXT("Common filters: Shadow, Lumen, TSR, Nanite, BasePass, Translucency, PostProcessing, VolumetricFog\n")
	TEXT("Channels: gpu,frame is minimum for GPU analysis; add cpu for full CPU trace\n")
	TEXT("The trace path is returned in the response — save it for subsequent analyze calls\n")
	TEXT("Multiple analyze calls on same trace are fast (parsed once)\n")
	TEXT("For A/B testing: always capture baseline first, change ONE setting, capture again, compare, then reset\n")
	TEXT("Use execute action=get_cvar to read current values before changing");

// ============================================================================
// Registry
// ============================================================================

static const FMCPSkillData RegisteredSkills[] =
{
	{
		TEXT("materials"),
		TEXT("Material Creation & Editing"),
		TEXT("How to create materials, add expression nodes, set properties, wire connections to material inputs, and compile. Covers the full workflow from empty asset to working material."),
		TEXT("Tools: create, graph, inspect"),
		MaterialsSteps, UE_ARRAY_COUNT(MaterialsSteps),
		MaterialsTips
	},
	{
		TEXT("blueprints"),
		TEXT("Blueprint Logic & Structure"),
		TEXT("How to build Blueprint logic: create Blueprints, add variables, define function graphs, add and connect nodes in event graphs and function graphs, and compile."),
		TEXT("Tools: create, graph, inspect"),
		BlueprintsSteps, UE_ARRAY_COUNT(BlueprintsSteps),
		BlueprintsTips
	},
	{
		TEXT("profiling"),
		TEXT("GPU Performance Profiling"),
		TEXT("How to capture Unreal Insights traces, analyze GPU pass timings, filter by pass name, and compare before/after with CVar changes. Full workflow from capture to analysis."),
		TEXT("Tools: trace, execute (for CVar changes)"),
		ProfilingSteps, UE_ARRAY_COUNT(ProfilingSteps),
		ProfilingTips
	},
};

const FMCPSkillData* GetRegisteredSkills()
{
	return RegisteredSkills;
}

int32 GetRegisteredSkillCount()
{
	return UE_ARRAY_COUNT(RegisteredSkills);
}

} // namespace MCPToolHelp
