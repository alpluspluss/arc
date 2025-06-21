# Contributing Guidelines

Thanks for your interest in contributing to Arc! Before you contribute, you should
familiarize yourself with the Arc project directory structure. In short,
Arc is a small, type-safe compiler backend in C++26.

For testing, the native version of Arc IR is built using CMake and the tests are run using
GoogleTest, which can be found via the system package manager or build from the source itself.

### Contents

- [Code Style](#code-style)
- [Submitting Pull Requests](#submitting-pull-requests)
- [Testing](#testing)
- [Commit Messages](#commit-messages)
- [Issue Guidelines](#issue-guidelines)

#### Code Style

Our code follows a consistent style to maintain readability and coherence across the project.
We use tabs for indentation (equivalent to 4 spaces in your editor). Types and classes should
use PascalCase naming (e.g., `PassContext`), while variables, functions, and methods
use snake_case (e.g., `intern_string`). Constants and enum values should be
in UPPER_SCREAMING_SNAKE_CASE. Member variables have no special prefix
and should be treated like normal symbols.

C-style casts are strictly forbidden regardless of how trivial the conversion might
seem; always use C++ style casts like `static_cast`, `reinterpret_cast`, etc.
Curly braces for control structures belong on their own line.
Template parameters should use PascalCase, with single letters for simple concepts
where appropriate. Keep line length reasonable, never exceeding 120 characters. Group
related functionality within namespaces, but avoid nesting namespaces except for
anonymous ones. When including headers, place STL headers before project
headers and always sort them alphabetically. Use `auto` when the type is
obvious from the right side of the assignment. Manual memory allocation
is forbidden without explicit permission from a maintainer. Always prefer modern
C++ features over their C counterparts (e.g., `using` over `typedef`, `nullptr` over `NULL`).

Comments should follow C-style format with lowercase text and
no capitalization (e.g., `/* this is a comment */`). While comments explaining
complex logic are welcome, self-documenting code is preferred. It is also important
to note that all includes should be in the form of `#include <header>` or `#include "header"` for
private headers in source directory (`src` or `lib`). Avoid using `#include <...>` for
private headers as this can lead to confusion.

All files are required to have license clause at the top of the file. Doxygen comments are
exceptions to the "all comments are lowercase" rule and is mandatory for public APIs. You
can however, leave private APIs undocumented but the self-documenting code rule shall still apply.

```cpp
/* this project is part of the Arc project; licensed under the MIT license. see LICENSE for more info */

#pragma once

namespace arc
{
    /**
     * @brief A class that shows Arc's codestyle
     */
    class ANewClass
    {
    public:
        /**
         * @brief ANewClass constructor
         * @param name Name for the object
         * @return The newly constructed object of type `ANewClass`
         */
        ANewClass(std::string_view name) = default;
        
    private:
        void bar();
    };
}
```

#### Submitting Pull Requests

We are currently not accepting new features, focusing instead on stabilizing
and improving existing functionality. When submitting a pull request, ensure
the PR title matches the issue you're addressing. Always include the text `Closes #<id>`
*with a period* in your PR description to link and automatically close the related issue when merged.

Provide a clear explanation of the changes you've made and your reasoning behind
implementation choices. Reference any related issues with the # notation. After receiving
approval from reviewers, a maintainer will handle merging your PR. Remember that all
contributions must adhere to the code style guidelines outlined above and include
appropriate tests covering your changes.

#### Testing

All contributions must include appropriate tests that verify your changes work 
as expected and don't break existing functionality. Tests should be organized
following the project structure in the `tests/` directory. Use GoogleTest 
macros appropriately and consider edge cases in your test scenarios. 
Run the full test suite locally before submitting a PR to ensure all 
tests pass. Integration tests are particularly important for compiler
passes to verify correct interaction between components. For complex
test scenarios, set up appropriate fixtures and mock external dependencies when testing
in isolation. Thorough testing helps prevent regressions and ensures the stability of the codebase.

#### Commit Messages

Commit messages must follow the conventional commit format, which starts with a type
followed by a short description (e.g., fix: segfault in type-registry.cpp).
The first line serves as a summary and should be concise. If additional
details are needed, leave a blank line after the summary and provide more
information in the body. Types can be anything ranging from `fix`, `feat`,
`docs`, `style`, `refactor`, `perf`, `test`, `chore` to anything that makes sense. 
This makes the project history more readable
and facilitates automated changelog generation. Each commit should
represent a logical unit of change that can be understood in isolation.
Avoid combining unrelated changes in a single commit.

#### Issue Guidelines

When creating issues, follow the format `<topic>: <sub>` for titles. For example,
`optimization: advanced dce support` or `bug: null pointer in alias analysis`. This
consistent naming scheme helps maintainers quickly understand the
nature and scope of the issue. Provide a detailed description of the
problem, including steps to reproduce for bugs, or clear requirements for
enhancements. If possible, suggest potential approaches to address the issue.
Be responsive to questions from maintainers who might need additional information.
Well-documented issues are more likely to be addressed promptly.
