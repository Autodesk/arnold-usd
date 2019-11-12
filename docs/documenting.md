Documentation rules
===================

Documentation is put in special comments, directly in the code so that you can
refer to and update the documentation while you're parsing or editing the code.
Special pages, such as this one, can be created as .dox files. This also
implies the documentation is version controlled along with the code. Doxygen
can then automatically generate HTML files, LaTeX, CHM files or other formats.

Please refer to the [Doxygen manual](http://www.doxygen.org/manual.html) for
details about Doxygen tags and syntax.

### Doxygen conventions

Doxygen allows different styles to document the code but we'll stick to these
conventions, chosen for elegance and simplicity:

- Doxygen commands and tags are prefixed with an at-sign (@@)

- The first line, until the first dot or blank line, of a Doxygen comment
will be used as the brief one-line description (`JAVADOC_AUTOBRIEF` and
`QT_AUTOBRIEF` are on).

### Documentation guidelines

Always precede classes, methods and functions with a Doxygen-style comment.
For classes, describe its responsabilities and dependencies. For methods and
functions, describe what the function does *and* describe each individual
parameter, as well as the return value. Follow the syntax in this example:

```
/// Short description in one phrase (in this case: Add two numbers together).
///
/// Optionally, here goes a much more detailed description of the function.
/// This longer description can spawn multiple lines, include code samples, etc.
///
/// @param a This is the first floating point number to be added
/// @param b And this is the second number
/// @return  The sum a + b
float myAddFunction(float a, float b)
{
   // ...
}
```

Always add a short description for each header file after the license to
explain its purpose and use the `\@file` tag to tag the file for doxygen.

### Documenting C++ code

- Use the three slashes for C++ Doxygen comments (`///`). After member
documentation is done with `///<`.

- the first sentence will be the short description of what you are documenting.
The first sentence is ended by a dot (`.`) or a blank line. 

An example of a well documented header file is: renderDelegate/nodes/nodes.h