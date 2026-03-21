# Reference Engine Research Before Implementation

**Category**: architecture
**Tags**: architecture, research, ue5, godot, unity, methodology

## Description

Before implementing any new system, launch architect agents in parallel to analyze how UE5 (W:\\UE_5.6_Source), Godot (E:\\godot), and Unity solve the same problem. Synthesize findings with engine-architect into UzEngine-specific directives. This prevents reinventing wheels and produces production-quality patterns. Architect agents run on haiku (cheap). Never rely solely on model knowledge — real engine source is ground truth.

## Example

# Example: Implementing clustered shading\n1. Launch unreal-architect: \"How does UE5 implement clustered/tiled shading?\"\n2. Launch godot-architect: \"How does Godot 4 handle light culling?\"\n3. Launch unity-architect: \"How does Unity URP/HDRP handle light clustering?\"\n4. Launch engine-architect: Synthesize → UzEngine directive\n5. Implement based on synthesized directive

## When to Use

Before any new feature/system implementation. Before debugging a hard problem (check reference engines for edge cases). When making architectural decisions that affect multiple subsystems.

## When NOT to Use

For trivial bug fixes where the solution is obvious. For UzEngine-specific systems with no engine equivalent.
