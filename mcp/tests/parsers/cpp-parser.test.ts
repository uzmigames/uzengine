import { describe, it, expect } from "vitest";
import { parseCppFile, extractIncludes } from "../../tools/parsers/cpp-parser.js";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";

describe("parseCppFile — synthetic", () => {
  it("parses a class with sections and methods", () => {
    const source = `#pragma once

/// A simple manager.
///
/// # Design
/// Single-threaded only.
///
/// # Thread Safety
/// Not thread-safe.
class FooManager final : public ISubsystem {
public:
    /// Does the thing.
    ///
    /// # Returns
    /// True on success.
    [[nodiscard]] bool DoThing();

    /// Sets the value.
    ///
    /// # Arguments
    /// * \`val\` - The new value. Must be > 0.
    void SetValue(float val);

private:
    float value_ = 1.0f; ///< Current value.
};`;

    const entities = parseCppFile(source, "test/foo.h");

    // Class entity
    const cls = entities.find((e) => e.name === "FooManager");
    expect(cls).toBeDefined();
    expect(cls!.kind).toBe("class");
    expect(cls!.brief).toBe("A simple manager.");
    expect(cls!.sections["Design"]).toContain("Single-threaded");
    expect(cls!.sections["Thread Safety"]).toContain("Not thread-safe");
    expect(cls!.relationships).toContainEqual({
      kind: "inherits",
      target: "ISubsystem",
    });

    // Methods
    const doThing = entities.find((e) => e.name === "DoThing");
    expect(doThing).toBeDefined();
    expect(doThing!.kind).toBe("method");
    expect(doThing!.returns).toContain("True on success");

    const setValue = entities.find((e) => e.name === "SetValue");
    expect(setValue).toBeDefined();
    expect(setValue!.params).toHaveLength(1);
    expect(setValue!.params![0].name).toBe("val");
    expect(setValue!.params![0].description).toContain("Must be > 0");

    // Trailing comment field
    const value = entities.find((e) => e.name === "value");
    expect(value).toBeDefined();
    expect(value!.kind).toBe("field");
    expect(value!.brief).toBe("Current value.");
    expect(value!.defaultValue).toBe("1.0f");
  });

  it("parses struct with @ref citations", () => {
    const source = `/// Physical atmosphere params.
///
/// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — Mie absorption
struct AtmoComponent {
    /// Mie absorption in km^-1.
    /// @ref Bruneton 2008 "Precomputed Atmospheric Scattering" — Mie absorption term
    float mieAbsorption = 4.44e-4f;
};`;

    const entities = parseCppFile(source, "test/atmo.h");

    const comp = entities.find((e) => e.name === "AtmoComponent");
    expect(comp).toBeDefined();
    expect(comp!.kind).toBe("struct");
    expect(comp!.refs).toBeDefined();
    expect(comp!.refs!.length).toBeGreaterThanOrEqual(1);
    expect(comp!.refs![0].author).toBe("Bruneton");
    expect(comp!.refs![0].year).toBe("2008");
    expect(comp!.refs![0].title).toBe("Precomputed Atmospheric Scattering");

    const field = entities.find((e) => e.name === "mieAbsorption");
    expect(field).toBeDefined();
    expect(field!.defaultValue).toBe("4.44e-4f");
    expect(field!.refs).toBeDefined();
    expect(field!.refs![0].author).toBe("Bruneton");
  });

  it("parses enum class", () => {
    const source = `/// Init phase for subsystems.
enum class InitPhase : uint8_t {
    PreInit = 0, ///< CPU-only setup.
    Init = 1,    ///< GPU initialization.
    Start = 2,   ///< World setup.
};`;

    const entities = parseCppFile(source, "test/phase.h");

    const en = entities.find((e) => e.name === "InitPhase");
    expect(en).toBeDefined();
    expect(en!.kind).toBe("enum");
    expect(en!.brief).toBe("Init phase for subsystems.");
  });

  it("parses file-level doc block", () => {
    const source = `/// UzEngine Time Manager
///
/// Tracks frame timing.

#pragma once
#include <chrono>

class TimeManager {};`;

    const entities = parseCppFile(source, "runtime/engine/app/time_manager.h");

    const fileDoc = entities.find((e) => e.kind === "file");
    expect(fileDoc).toBeDefined();
    expect(fileDoc!.brief).toBe("UzEngine Time Manager");
    expect(fileDoc!.description).toContain("Tracks frame timing");
  });

  it("parses #define macros", () => {
    const source = `/// Logs a message at the given level.
#define UZ_LOG(category, level, ...) LogImpl(category, level, __VA_ARGS__)`;

    const entities = parseCppFile(source, "test/macros.h");

    const macro = entities.find((e) => e.name === "UZ_LOG");
    expect(macro).toBeDefined();
    expect(macro!.kind).toBe("macro");
  });

  it("parses example code blocks", () => {
    const source = `/// Does stuff.
///
/// # Example
/// \`\`\`cpp
/// auto x = doStuff(42);
/// \`\`\`
void doStuff(int n);`;

    const entities = parseCppFile(source, "test/stuff.h");

    const fn = entities.find((e) => e.name === "doStuff");
    expect(fn).toBeDefined();
    expect(fn!.examples).toHaveLength(1);
    expect(fn!.examples![0]).toContain("doStuff(42)");
  });

  it("parses @see cross-references", () => {
    const source = `/// Renders fog.
///
/// @see HeightFogComponent, FogCB
void RenderFog();`;

    const entities = parseCppFile(source, "test/fog.h");

    const fn = entities.find((e) => e.name === "RenderFog");
    expect(fn).toBeDefined();
    expect(fn!.seeAlso).toEqual(["HeightFogComponent", "FogCB"]);
  });
});

describe("parseCppFile — real files", () => {
  it("parses time_manager.h", () => {
    const path = resolve("../runtime/engine/app/time_manager.h");
    let source: string;
    try {
      source = readFileSync(path, "utf-8");
    } catch {
      // Skip if file not available (CI without full repo)
      return;
    }

    const entities = parseCppFile(
      source,
      "runtime/engine/app/time_manager.h",
    );

    // File-level doc
    const fileDoc = entities.find((e) => e.kind === "file");
    expect(fileDoc).toBeDefined();
    expect(fileDoc!.brief).toContain("UzEngine Time Manager");

    // Class
    const cls = entities.find((e) => e.name === "TimeManager");
    expect(cls).toBeDefined();
    expect(cls!.kind).toBe("class");
    expect(cls!.sections["Thread Safety"]).toBeDefined();
    expect(cls!.relationships).toContainEqual({
      kind: "inherits",
      target: "ISubsystem",
    });

    // Methods
    const beginFrame = entities.find((e) => e.name === "BeginFrame");
    expect(beginFrame).toBeDefined();
    expect(beginFrame!.sections["Behavior"]).toBeDefined();

    const consumeFixed = entities.find((e) => e.name === "ConsumeFixedTicks");
    expect(consumeFixed).toBeDefined();
    expect(consumeFixed!.returns).toBeDefined();

    const setFixed = entities.find((e) => e.name === "SetFixedDeltaTime");
    expect(setFixed).toBeDefined();
    expect(setFixed!.params).toBeDefined();
    expect(setFixed!.params![0].name).toBe("dt");
  });

  it("parses sky_atmosphere_component.h", () => {
    const path = resolve(
      "../runtime/engine/ecs/environment/sky_atmosphere_component.h",
    );
    let source: string;
    try {
      source = readFileSync(path, "utf-8");
    } catch {
      return;
    }

    const entities = parseCppFile(
      source,
      "runtime/engine/ecs/environment/sky_atmosphere_component.h",
    );

    // Struct
    const comp = entities.find((e) => e.name === "SkyAtmosphereComponent");
    expect(comp).toBeDefined();
    expect(comp!.kind).toBe("struct");

    // Fields with defaults
    const bottomRadius = entities.find((e) => e.name === "bottomRadiusKm");
    expect(bottomRadius).toBeDefined();
    expect(bottomRadius!.defaultValue).toBe("6360.0f");

    // @ref citations
    const mieAbs = entities.find((e) => e.name === "mieAbsorption");
    expect(mieAbs).toBeDefined();
    expect(mieAbs!.refs).toBeDefined();
    expect(mieAbs!.refs![0].author).toBe("Bruneton");
  });
});

describe("extractIncludes", () => {
  it("extracts quoted includes", () => {
    const source = `#include "foo.h"
#include <vector>
#include "bar/baz.h"`;

    expect(extractIncludes(source)).toEqual(["foo.h", "bar/baz.h"]);
  });
});
