# Hyphenation patterns

`hyph-en-us.tex` — Liang hyphenation patterns for American English, from the
[hyph-utf8](http://www.hyphenation.org/tex) package.

    Copyright (C) 1990, 2004, 2005 Gerard D.C. Kuiken

    Copying and distribution of this file, with or without modification,
    are permitted in any medium without royalty provided the copyright
    notice and this notice are preserved.

That notice is preserved in the file itself and reproduced in `LICENSE`.
It is the FSF all-permissive licence and imposes nothing on the rest of the
project.

The file also documents the hyphenmins the patterns were designed for — two
characters before the first break, three after the last — which is where
`HyphenLimits` gets its defaults.

## Regenerating the tables

`src/core/layout/hyphen_patterns_en.cpp` is generated from this file and is
checked in, so no build system needs a codegen step:

```sh
make bin/hyphgen
./bin/hyphgen assets/hyphenation/hyph-en-us.tex src/core/layout/hyphen_patterns_en.cpp
```

Do not edit the generated file by hand.
