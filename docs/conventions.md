Code Conventions
==================================

English is the official language to use in the code and comments. Also please
use the US spelling and not the UK one (ie. *colorize*, not *colourise*).

Use only ASCII (Latin-1, ISO-8859-1) characters anywhere in the code,
comments or file names, this means no accents and special characters.   

For Python, generally follow the [PEP-8 guidelines](http://www.python.org/dev/peps/pep-0008/).

Use the supplied .clang-format (for clang format 7) and the scripts to format the codebase.

## File names

We use snake_case for file and folder names to avoid issues between windows and unix style file systems. Header files use the "h" extension, source files use "cpp".

```
contrib // OK
renderDelegate.h // WRONG
render_delegate.cpp // OK
render_delegate.cxx // WRONG
```

## Naming conventions

#### We follow USD's naming conventions.

#### Classes are named CamelCase, starting with a module specific prefix unless they are not in a header. Struct is used instead of a class when storing related data in a compact way, without visibility or member functions and they only need the module specific prefix if exposed in a header.


```cpp
// Base library
class HdArnoldLight; // OK
class MyClass; // WRONG
class myClass; // WRONG

struct MyStruct : public HdRenderDelegate {
    // ... 
}; // WRONG

struct MyOtherStruct {
    float a;
    int b;
    double c;
} // OK
```

#### Functions are also CamelCase, private and protected functions on classes are prefixed with an _ .

```cpp
// Header file
void MyFunction(); // OK
void myFunction(); // WRONG
void my_function(); // WRONG

class HdArnoldRenderDelegate {
public:
    void MyFunction(); // OK
private:
    void MyOtherFunction(); // WRONG
    void _MyFunction(); // OK
};
```

#### Source only functions and structs are placed in an anonymous namespace to limit symbols to compilation units and source only functions are prefixed with an _ . Anoymous namespaces do NOT increase identation tot he right.

```cpp
// Source file

namespace {

void MyFunction()
{
    // ...
} // WRONG

void _MyFunction()
{
    // ...
} // OK

}

```

#### Variables are camelCase, class variables are prefixed with an _ . Struct variables are not prefixed with an _ .

```cpp
int myVariable; // OK
int my_variable; // WRONG

class HdArnoldRenderDelegate {
private:
    int myVariable; // WRONG
    int _myVariable; // OK
};

struct MyStruct {
    int _myVariable; // WRONG
    int myVariable; // OK
};
```

#### Macros and enums are all upper case.

```cpp
#define UPDATE_UNDEFINED   0
#define UPDATE_CAMERA      1

enum PartitionType
{
    POINTS = 0,
    PRIMITIVES,
    DETAIL
};
```


## Coding style

#### We use C++11 to follow USD's choice of standard and respect the [C++ core guidelines](http://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines.html).

#### Some highlights.

#### Maximum column width for source files is 120. Use short lines (~80 to follow USD's convention), unless indenting becomes hard to follow.

#### There is no space between the name of a method or function and its parenthesis, neither between the parenthesis and the arguments.

```cpp
myFunction(a, b);    // OK
myFunction (a, b);   // wrong
myFunction( a, b );  // wrong
```

#### End source files with an empty line.

#### Use const and constexpr wherever possible.

#### Use nullptr in place of NULL or 0 for pointers.

```cpp
A* a = nullptr; // OK
B* b = 0; // WRONG
C* c = NULL; // WRONG
```

#### Use override for class functions.

#### Use C++ style casting.

#### Use C++ notation for include files.

```cpp
#include <cstring>   // OK
#include <string.h>  // WRONG
```

#### Use C++ comments (`//`), not C comments (`/* ... \*\/`). There must be a space following the `//`.

```cpp
// I am a nice comment.
//But I am not.
/* I'm ugly too. */
```

#### Indent with four (4) real spaces, no tabs.

#### Brackets are always required for every block of code, to avoid possible mistakes, like problems with macros.

```cpp
if (statement)
    OneLineFunction(); // WRONG

if (statement) {
    OneLineFunction(); // OK
}
```
