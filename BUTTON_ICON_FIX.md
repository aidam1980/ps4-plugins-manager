# Button legend image fix

The button legend renderer now uses embedded RGBA pixel arrays generated from the real PNG button artwork. It no longer depends on loading PNG files from `/app0/assets/buttons` at runtime, and it does not draw text-keycap fallbacks. This specifically addresses the screenshot where `[X]`, `[SQ]`, and `R3` were displayed instead of button images. Rebuild the PKG and reinstall it to test.
