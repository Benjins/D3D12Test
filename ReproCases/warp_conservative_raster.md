When conservative rasterization is enabled, and a vertex shader outputs a triangle with 2 vertices at the same position, an out of bounds access violation occurs.

Repros in Version 1909, fixed in Version 2004.
