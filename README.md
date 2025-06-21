<div align="center">
  <img src="assets/logo.png" alt="Arc Logo" width="35%">
  <h1>Arc</h1>
  <p><i>A high-performance compiler infrastructure in C++26</i></p>
  <p>
    <img src="https://github.com/alpluspluss/arc/actions/workflows/build.yml/badge.svg" alt="Build & Unit Tests">
    <img src="https://img.shields.io/badge/License-MIT-blue.svg" alt="License">
    <img src="https://img.shields.io/badge/C%2B%2B-26-00599C.svg" alt="C++ Version">
  </p>
</div>

This repository contains the source code for Arc; a modern, type-safe and moderately small 
compiler backend.

Arc uses a graph-based intermediate representation to provide a flexible foundation 
for building optimizing compilers with customizable passes. It is designed to be 
easy to use and comprehensible while being fast and efficient.

## Getting Started

To build Arc, you need to have CMake 3.30+ and a C++26 compatible compiler.
MSVC is not supported.

```shell
git clone https://github.com/alpluspluss/arc.git
cd arc
mkdir build
cmake -B build                   \
  -DCMAKE_BUILD_TYPE=Release     \
  -DARC_BUILD_EXAMPLES=OFF       \
  -DARC_BUILD_TESTS=OFF          \
  -DARC_BUILD_BENCHMARKS=OFF     \
cmake --build build
```

#### Available Build Options

| Option                 | Description                | Default |
|------------------------|----------------------------|---------|
| `ARC_BUILD_BENCHMARKS` | Build performance tests    | `ON`    |
| `ARC_BUILD_EXAMPLES`   | Build example programs     | `ON`    |
| `ARC_BUILD_TESTS`      | Build unit tests           | `ON`    |
| `ARC_BUILD_SHARED_LIB` | Build shared libraries     | `ON`    |
| `ARC_INSTALL`          | Enable installation        | `ON`    |

#### Development Build Options

| Option                  | Description              | Default |
|-------------------------|--------------------------|---------|
| `ARC_ENABLE_SANITIZERS` | Enable ASAN + UBSAN      | `OFF`   |
| `ARC_ENABLE_ASAN`       | Enable AddressSanitizer  | `OFF`   |
| `ARC_ENABLE_TSAN`       | Enable ThreadSanitizer   | `OFF`   |
| `ARC_ENABLE_MSAN`       | Enable MemorySanitizer   | `OFF`   |
| `ARC_ENABLE_UBSAN`      | Enable UBSanitizer       | `OFF`   |
| `ARC_STRICT_WARNINGS`   | Treat warnings as errors | `OFF`   |

## Quick Start

```cpp
#include <arc/foundation/module.hpp>
#include <arc/foundation/builder.hpp>

int main()
{
    auto module = arc::Module("Module");
    auto builder = arc::Builder(module);
    
    auto main_fn = builder.function("main")
        .returns<arc::DataType::INT32>();
        .body([&](auto& fb)
        {
            auto val1 = fb.lit(42);
            auto val2 = fb.lit(58);
            return fb.add(val1, val2);
        });
    return 0;
}
```

## Library Structure

Arc is organized into focused libraries that can be used independently.

```cmake
find_package(Arc REQUIRED)

# use specific layers
target_link_libraries(my_frontend Arc::Foundation)
target_link_libraries(my_optimizer Arc::Transform)

# use everything
target_link_libraries(my_compiler Arc::Arc)
```

## Status

The project is in early development. The API is unstable and may subject to change in the future.

| Component           | Status      | Notes                               |
|---------------------|-------------|-------------------------------------|
| Core Infrastructure | Complete    | Modules, nodes, type system         |
| Memory Management   | Complete    | Thread-local allocation pools       |
| Type System         | Complete    | Direct relationships, no registries |
| Pass Infrastructure | In Progress | Dependency tracking, execution      |
| Analysis Passes     | In Progress | Alias analysis, dominance           |
| Transform Passes    | In Progress | DCE, CSE, vectorization             |
| IPO Passes          | Planned     | Inlining, global optimizations      |
| Code Generation     | Planned     | LLVM backend integration            |

## License

This project is licensed under the MIT License. See the [LICENSE](CONTRIBUTING) file for details.

## Author

- Al (@alpluspluss)

## Contributing

Arc is currently in development. Contributions are welcome through issues and pull requests.
Please follow the [contributing guidelines](CONTRIBUTING.md) for code style and submission requirements.

## Acknowledgements

- [LLVM Project](https://github.com/llvm/llvm-project) for excellent documentation on compiler infrastructure
- [Acheron library](https://github.com/deviceix/acheron/tree/main) for an awesome memory allocator.
