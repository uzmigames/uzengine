import { describe, it, expect } from "vitest";
import {
  splitIdentifier,
  tokenize,
  removeStopWords,
  tokenizeFull,
  isStopWord,
} from "../../src/search/tokenizer.js";

describe("splitIdentifier", () => {
  it("splits CamelCase", () => {
    expect(splitIdentifier("TimeManager")).toEqual(["time", "manager"]);
  });

  it("splits PascalCase with acronyms", () => {
    expect(splitIdentifier("HTMLParser")).toEqual(["html", "parser"]);
  });

  it("splits snake_case", () => {
    expect(splitIdentifier("sky_atmosphere_ps")).toEqual([
      "sky",
      "atmosphere",
      "ps",
    ]);
  });

  it("splits mixed case with abbreviations", () => {
    expect(splitIdentifier("PerFrameCB")).toEqual(["per", "frame", "cb"]);
  });

  it("handles single word", () => {
    expect(splitIdentifier("Renderer")).toEqual(["renderer"]);
  });

  it("handles all-caps", () => {
    expect(splitIdentifier("HLSL")).toEqual(["hlsl"]);
  });

  it("handles numbers in identifiers", () => {
    expect(splitIdentifier("Vec3")).toEqual(["vec3"]);
  });

  it("handles empty string", () => {
    expect(splitIdentifier("")).toEqual([]);
  });

  it("handles dots and colons", () => {
    expect(splitIdentifier("foo.bar::baz")).toEqual(["foo", "bar", "baz"]);
  });
});

describe("tokenize", () => {
  it("lowercases and splits on non-alphanumeric", () => {
    expect(tokenize("Hello World 123")).toEqual(["hello", "world", "123"]);
  });

  it("preserves underscores within tokens", () => {
    expect(tokenize("fog_density is_valid")).toEqual([
      "fog_density",
      "is_valid",
    ]);
  });

  it("filters single-character tokens", () => {
    expect(tokenize("a b cd ef")).toEqual(["cd", "ef"]);
  });

  it("handles empty string", () => {
    expect(tokenize("")).toEqual([]);
  });

  it("handles special characters", () => {
    expect(tokenize("@ref Bruneton 2008")).toEqual(["ref", "bruneton", "2008"]);
  });
});

describe("removeStopWords", () => {
  it("removes common stop words", () => {
    expect(removeStopWords(["the", "fog", "is", "dense"])).toEqual([
      "fog",
      "dense",
    ]);
  });

  it("preserves technical terms", () => {
    expect(
      removeStopWords(["cbuffer", "per", "frame", "constants"]),
    ).toEqual(["cbuffer", "per", "frame", "constants"]);
  });

  it("handles all stop words", () => {
    expect(removeStopWords(["the", "a", "an", "is"])).toEqual([]);
  });
});

describe("tokenizeFull", () => {
  it("combines tokenization and stop word removal", () => {
    expect(tokenizeFull("The fog density is computed per frame")).toEqual([
      "fog",
      "density",
      "computed",
      "per",
      "frame",
    ]);
  });
});

describe("isStopWord", () => {
  it("identifies stop words", () => {
    expect(isStopWord("the")).toBe(true);
    expect(isStopWord("is")).toBe(true);
  });

  it("rejects non-stop words", () => {
    expect(isStopWord("fog")).toBe(false);
    expect(isStopWord("renderer")).toBe(false);
  });
});
