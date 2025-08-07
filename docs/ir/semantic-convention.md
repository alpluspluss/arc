# Arc IR Semantic Conventions

This document describes the semantic behavior of Arc IR operations when applied to different data types, 
complementing the operand ordering conventions.

## Type Interaction Rules

### Arithmetic Operations on Vectors

**Element-wise semantics**: All arithmetic operations on vectors apply element-wise across corresponding elements.

```cpp
ADD[vector<4 x float>, vector<4 x float>] → vector<4 x float>  /* [a0+b0, a1+b1, a2+b2, a3+b3] */
SUB[vector<2 x int32>, vector<2 x int32>] → vector<2 x int32>  /* [a0-b0, a1-b1] */
MUL[vector<8 x uint8>, vector<8 x uint8>] → vector<8 x uint8>  /* element-wise multiplication */
```

**Vector-scalar operations**: Operations between vector and scalar broadcast the scalar to all elements.

```cpp
ADD[vector<4 x float>, float] → vector<4 x float>  /* [v0+s, v1+s, v2+s, v3+s] */
MUL[vector<2 x int32>, int32] → vector<2 x int32>  /* [v0*s, v1*s] */
```

**Vector size constraints**: Vector operations require identical element counts and compatible element types.

```cpp
ADD[vector<4 x float>, vector<2 x float>]  /* ERROR - mismatched vector sizes */
ADD[vector<4 x float>, vector<4 x int32>]  /* ERROR - incompatible element types */
```

### Comparison Operations on Vectors

**Element-wise comparisons**: Comparison operations on vectors produce vector results with boolean elements.

```cpp
EQ[vector<4 x float>, vector<4 x float>] → vector<4 x bool>   /* [a0==b0, a1==b1, a2==b2, a3==b3] */
LT[vector<2 x int32>, vector<2 x int32>] → vector<2 x bool>   /* [a0<b0, a1<b1] */
```

## Pointer Arithmetic Semantics

### Byte-Level Addressing

**Byte offset arithmetic**: `PTR_ADD` operates on byte-level offsets, not element counts.

```cpp
PTR_ADD[ptr_to_int32, 4] → ptr_to_int32    /* advances by 4 bytes; 1 int32 element */
PTR_ADD[ptr_to_float, 8] → ptr_to_float    /* advances by 8 bytes; 2 float elements */
PTR_ADD[ptr_to_uint8, 1] → ptr_to_uint8    /* advances by 1 byte */
```

**Type preservation**: Pointer arithmetic preserves the pointee type information.

```cpp
PTR_ADD[ptr_to_struct, offset] → ptr_to_struct  /* pointee type unchanged */
PTR_ADD[ptr_to_vector, offset] → ptr_to_vector  /* vector pointee preserved */
```

**Address space preservation**: Pointer operations maintain address space attributes.

```cpp
PTR_ADD[ptr_addr_space_1, offset] → ptr_addr_space_1  /* address space preserved */
```

## Type Promotion Conventions

### Integer Promotion Rules

**Small integer promotion**: Operations involving `BOOL`, `INT8`, `UINT8`, `INT16`, or `UINT16` promote both operands to `INT32`.

```cpp
ADD[INT8, UINT16] → INT32      /* both operands promoted to INT32 */
ADD[BOOL, INT16] → INT32       /* both operands promoted to INT32 */
MUL[UINT8, UINT8] → INT32      /* both operands promoted to INT32 */
```

**Large integer operations**: `INT32`, `UINT32`, `INT64`, and `UINT64` operations preserve the larger or more precise type.

```cpp
ADD[INT32, UINT32] → UINT32    /* unsigned takes precedence */
ADD[INT32, INT64] → INT64      /* larger size takes precedence */
ADD[UINT32, INT64] → INT64     /* signed 64-bit takes precedence over unsigned 32-bit */
```

### Floating-Point Promotion

**Float dominance**: Any operation involving floating-point types promotes the result to floating-point.

```cpp
ADD[INT32, FLOAT32] → FLOAT32  /* integer promotes to float */
MUL[UINT64, FLOAT64] → FLOAT64 /* integer promotes to double */
```

**Float precision rules**: Operations between different float types promote to the higher precision.

```cpp
ADD[FLOAT32, FLOAT64] → FLOAT64  /* single precision promotes to double */
DIV[FLOAT32, FLOAT32] → FLOAT32  /* same precision preserved */
```

## Cast Operation Semantics

### Explicit Type Conversion

**Truncation casts**: Casting to smaller integer types truncates high-order bits.

```cpp
CAST<UINT8>[INT32] /* keeps low 8 bits, discards upper 24 bits */
CAST<INT16>[INT64] /* keeps low 16 bits, discards upper 48 bits */
```

**Sign extension**: Casting signed integers to larger types sign-extends.

```cpp
CAST<INT32>[INT8]  /* sign-extends from 8 to 32 bits */
CAST<INT64>[INT16] /* sign-extends from 16 to 64 bits */
```

**Float-integer conversion**: Follows standard IEEE 754 conversion rules.

```cpp
CAST<INT32>[FLOAT32]  /* truncates fractional part */
CAST<FLOAT64>[INT64]  /* converts to nearest representable double */
```

### Pointer Casts

**Address-preserving**: Pointer casts change type information while preserving the address value.

```cpp
CAST<ptr_to_uint8>[ptr_to_int32]  /* same address, different pointee type */
```

## Call Semantics

The `CALL` node uses types to determine both direct and indirect call (function pointer) semantics.

For direct calls, the function operand must be a function type. For indirect calls, 
the function operand has `DataType::POINTER` with `pointee->type_kind == DataType::FUNCTION`.

The call node's type_kind field stores the return type extracted from the function signature. 
No additional type metadata shall be stored in the call node's value field as the function
node itself maintains all signature information.

## Memory Operation Semantics

### Load/Store Size Determination

**Type-driven sizing**: Memory operations use the target type to determine access size.

```cpp
LOAD[location] → INT32     /* loads 4 bytes */
LOAD[location] → FLOAT64   /* loads 8 bytes */
PTR_LOAD[ptr_to_uint8]     /* loads 1 byte */
```

### Atomic Operation Constraints

**Size restrictions**: Atomic operations are typically restricted to naturally-aligned power-of-2 sizes.

```cpp
ATOMIC_LOAD[ptr_to_int32]   /* 4-byte atomic load */
ATOMIC_LOAD[ptr_to_int64]   /* 8-byte atomic load */
ATOMIC_STORE[value, ptr_to_uint16]  /* 2-byte atomic store */
```

**Ordering semantics**: Memory ordering constraints apply to the entire operation.

```cpp
ATOMIC_LOAD[address, SEQ_CST]                    /* sequentially consistent load */
ATOMIC_CAS[address, expected, desired, ACQ_REL]  /* acquire-release CAS */
```

## FROM Node Semantics

The `FROM` node represents value merging at control flow join points, typically created during SSA 
construction (SROA/Mem2Reg). All source operands must have identical types.

```cpp
/* example: Variable promoted from memory to register */
%result = i32 FROM [%else_value, %then_value]    /* order doesn't matter */
%result = i32 FROM [%then_value, %else_value]    /* equivalent */
```

## Error Conditions

### Type Compatibility Errors

Operations that violate type compatibility rules should be rejected.

```cpp
ADD[ptr_to_int32, ptr_to_float]    /* ERROR: pointer arithmetic between different types */
LOAD[FUNCTION]                     /* ERROR: cannot load from function type */
PTR_STORE[value, INT32]            /* ERROR: cannot store through non-pointer */
```

### Vector Constraint Violations

Vector operations must satisfy dimensional and type constraints.

```cpp
VECTOR_EXTRACT[vector<4 x float>, 5]  /* ERROR: index out of bounds */
ADD[vector<3 x int>, vector<4 x int>] /* ERROR: mismatched vector dimensions */
```

These semantic conventions try to ensure consistency across all Arc IR operations.
