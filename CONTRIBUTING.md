Contributing to Arnold USD
=======================

# Do’s and Don’ts

* **Search tickets before you file a new one.** Add to tickets if you have new information about the issue.
* **Keep tickets short but sweet.** Make sure you include all the context needed to solve the issue. Don't overdo it. Great tickets allow us to focus on solving problems instead of discussing them.
* **Take care of your ticket.** When you spend time to report a ticket with care we'll enjoy fixing it for you.
* **Use [GitHub-flavored Markdown](https://help.github.com/articles/markdown-basics/).** Especially put code blocks and console outputs in backticks (```` ``` ````). That increases the readability.
* **Do not litter.** Don’t add +1’s _unless_ specifically asked for and don’t discuss offtopic issues.

## Bug Reports

In short, since you are most likely a developer, provide a ticket that you
_yourself_ would _like_ to receive.

We depend on _you_ (the community) to contribute in making the project better
for everyone. So debug and reduce your own issues before creating a ticket and
let us know of all the things you tried and their outcome. This applies double
if you cannot share a reproduction with us because of internal company policies.

Please include steps to reproduce and _all_ other relevant information,
including any other relevant dependency and version information.

## Feature Requests

Please try to be precise about the proposed outcome of the feature and how it
would related to existing features.

## Pull Requests

We **love** pull requests!

All contributions _will_ be licensed under the Apache 2.0 license.

Code/comments should adhere to the following rules:

* All changes require test coverage (where it's applicable) to ensure it does
  not break during refactor work, as well as ensure consistent behavior across
  supported platforms.
* Comments are required for public APIs. See Documentation Conventions below.
* When documenting APIs and/or source code, don't make assumptions or make
  implications about race, gender, religion, political orientation, or anything
  else that isn't relevant to the project.
* Remember that source code usually gets written once and read often: ensure
  the reader doesn't have to make guesses. Make sure that the purpose and inner
  logic are either obvious to a reasonably skilled professional, or add a
  comment that explains it.
* Pull requests will be squashed and merged. Please add a detailed description;
  it will be used in the commit message and paraphrased in release notes.

## More Details

For coding and documentation conventions check out these sub-pages:

[Code Conventions](docs/conventions.md)

[Documentation Conventions](docs/documenting.md)

[Arnold-USD Naming Conventions](docs/naming_conventions.md)
