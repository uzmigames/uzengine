# TODOs, Stubs, and Placeholders in Implementation

**Category**: code
**Tags**: quality, no-shortcuts, no-todos, no-stubs, critical

## Description

Delivering code with TODOs, stubs, placeholder values, or simplified logic to reduce response time. This includes: "// TODO: implement", stub functions that return dummy values, placeholder constants, simplified algorithms that skip edge cases, and partial implementations that leave work for later.

## Example

// ❌ ABSOLUTELY FORBIDDEN
void ComputeGI() {
    // TODO: implement global illumination
}

float CalculateFresnel(float cosTheta) {
    return 0.5f; // placeholder
}

void BuildBVH(const std::vector&lt;Triangle&gt;&amp; tris) {
    // stub: just use linear search for now
    m_triangles = tris;
}

## When to Use

Never. This anti-pattern should never be used under any circumstances.

## When NOT to Use

Always avoid. Response time is irrelevant — quality is the only metric that matters.
