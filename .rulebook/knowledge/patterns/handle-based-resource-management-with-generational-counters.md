# Handle-Based Resource Management with Generational Counters

**Category**: architecture
**Tags**: memory, handles, resources, safety, use-after-free

## Description

All GPU and persistent resources use HandleMap<T> with generational counters. No raw pointers cross abstraction boundaries. When a resource is destroyed, its generation increments. Holders of old handles get 'invalid' on access rather than use-after-free. This pattern is used for RHI resources (buffers, textures, pipelines), ECS entities (via EnTT), and Lua entity references (64-bit packed lightuserdata).

## Example

// HandleMap<T> usage:\nauto handle = handleMap.Create(resourceData);\nbool valid = handleMap.IsValid(handle); // checks generation\nauto* ptr = handleMap.Get(handle); // nullptr if generation mismatch\nhandleMap.Destroy(handle); // increments generation\n// Old handle now returns nullptr on Get()

## When to Use

For any resource with create/destroy lifecycle that may be referenced from multiple locations. For cross-thread resource references. For any handle passed to Lua or serialized.

## When NOT to Use

For stack-local temporaries. For resources with guaranteed lifetime matching their owner (use unique_ptr).
