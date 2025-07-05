# Arc IR Operand Convention

Arc IR follows a consistent operand ordering pattern across all operations to create a predictable and intuitive programming model.

## Core Principles

The operand convention is designed with these rationales:

- **Data flows from inputs to outputs; source before destination**
- **Primary input comes first because it's the most important**
- **Same pattern across operations of the same type**
- **Type-driven semantics so we can determine behaviors of nodes by operand types**

## Type-Driven Dispatch

Types of the address operand are used to determine memory access semantics without ambiguity.

```cpp
Node* address = /* ... */;
if (address->type_kind == DataType::POINTER) 
{
    /* use PTR_LOAD/PTR_STORE (indirect access) */
}
else 
{
    /* use LOAD/STORE (direct access to named location) */
}
```

## Operand Patterns by Category

### Arithmetic Operations
**Pattern**: `[source1, source2] → result`
```
ADD:    [lhs, rhs]
SUB:    [lhs, rhs] 
MUL:    [lhs, rhs]
DIV:    [lhs, rhs]
MOD:    [lhs, rhs]
```
*Rationale: Mathematical notation convention (a + b)*

### Comparison Operations

**Pattern**: `[left_operand, right_operand] → bool`
```
EQ:     [lhs, rhs]
LT:     [lhs, rhs]
GT:     [lhs, rhs]
```
*Rationale: Matches mathematical comparison (a < b)*

### Memory Operations

**Pattern**: `[data, destination]` for stores, `[source] → data` for loads
```
STORE:      [value, location]     /* what you're storing, where it goes */
PTR_STORE:  [value, pointer]      /* same pattern, different addressing */
LOAD:       [location]            /* what you're loading from */
PTR_LOAD:   [pointer]             /* same pattern, different addressing */
```
*Rationale: Store follows assignment syntax (location = value), load is simple dereference*

### Pointer Operations

**Pattern**: Primary pointer first, then modifiers
```
PTR_ADD:   [base_pointer, offset]    /* base + offset */
ADDR_OF:   [variable]                /* &variable */
```
*Rationale: Base pointer is the primary data being operated on*

### Control Flow Operations

**Pattern**: Decision criteria first, then destinations
```
BRANCH:    [condition, true_target, false_target]    /* if-then-else order */
JUMP:      [target]                                  /* goto target */
CALL:      [function, arg1, arg2, ...]               /* callee first, then arguments */
INVOKE:    [function, normal_target, except_target, arg1, arg2, ...]
```
*Rationale:  Fixed target positions to enable O(1) parsing while variable arguments go at the end*

### Vector Operations

**Pattern**: Vector data first, then selectors/modifiers
```
VECTOR_BUILD:   [elem0, elem1, elem2, ...]     /* elements in order */
VECTOR_EXTRACT: [vector, index]                /* what you're extracting from, where */
VECTOR_SPLAT:   [scalar]                       /* what you're replicating */
```
*Rationale: Primary data (vector/scalar) comes first, indices are selectors*

### Atomic Operations

**Pattern**: Memory location first, then data, then ordering
```
ATOMIC_LOAD:  [address, ordering]                    /* where, how */
ATOMIC_STORE: [value, address, ordering]             /* what, where, how */
ATOMIC_CAS:   [address, expected, desired, ordering] /* where, compare, swap, how */
```
*Rationale: Memory location is primary, ordering is metadata*

### Cast Operations

**Pattern**: `[source] → target_type`
```
CAST: [value]    /* type is specified by node's type_kind field */
```
*Rationale: Simple transformation of one value*

### Allocation Operations

**Pattern**: Size specification leads to allocation
```
ALLOC: [size]    /* how much to allocate */
```
*Rationale: Size determines the allocation*

### Data Structure Access Operations

**Pattern**: `[container, selector] → element`
```
ACCESS: [struct_or_array, index]    /* what you're accessing, which element */
```
*Rationale: Container is primary data, index/field selector specifies which part*

## Type Safety Constraints

### Operand Type Constraints

1. **Operand types must be compatible with operation**
2. **Operand count must match the specification for each node type**
3. **POINTER types must have valid pointee information**
4. **Control flow targets must reference valid ENTRY nodes**
5. **Pointer operations must reference valid allocations for memory safety**
6. **Operation results must match declared types for consistency**

### Memory Safety Rules

- `LOAD`/`STORE` operations work with named memory locations (non-pointer types)
- `PTR_LOAD`/`PTR_STORE` operations work with pointer values (`DataType::POINTER`)
- Pointer arithmetic operations maintain pointee type information
- Address-of operations create typed pointers to their operands

## Consistency Examples

This convention creates predictable patterns:

```cpp
store_node = STORE[value, location]      /* direct */
ptr_store_node = PTR_STORE[value, ptr]   /* indirect */

add_node = ADD[lhs, rhs]
sub_node = SUB[lhs, rhs]

branch_node = BRANCH[condition, true_target, false_target]
call_node = CALL[function, arg1, arg2]
```

The consistent ordering means that once developers learn one operation type, 
they understand the pattern for all operations in that category.

## Builder API Correspondence

The builder API abstracts operand ordering while maintaining semantic consistency:

```cpp
/* these produce identical operand patterns */
builder.store(value, location);           /* STORE[value, location] */
builder.store(value).to(location);        /* STORE[value, location] */
builder.store(value).through(pointer);    /* PTR_STORE[value, pointer] */
builder.store(value).to_atomic(location); /* ATOMIC_STORE[value, location, ordering] */
```
