// Generated automatically from mod/list.oba. Do not edit.
const char* listModSource =

"// List is an ordered sequence of values.\n"
"// * Empty creates an empty list\n"
"// * Cons creates a list by adding x to the list, xs.\n"
"data List = Empty | Cons x xs\n"
"\n"
"// Append adds an item to the end of a list.\n"
"fn append x xs {\n"
"let os = Cons(x, Empty())\n"
"let rs = reverse(xs)\n"
"while rs != Empty() {\n"
"os = Cons(head(rs), os)\n"
"rs = tail(rs)\n"
"}\n"
"return os\n"
"}\n"
"\n"
"// Length returns the length of a list.\n"
"fn length xs  = _length(xs, 0)\n"
"fn _length xs len {\n"
"if xs == Empty() return len\n"
"return _length(tail(xs), len + 1)\n"
"}\n"
"\n"
"// Where returns the items of xs for which `f(x)` returns true.\n"
"fn where f xs {\n"
"if xs == Empty() return xs\n"
"\n"
"let x = head(xs)\n"
"xs = tail(xs)\n"
"\n"
"if f(x) return Cons(x, where(f, xs))\n"
"return where(f, xs)\n"
"}\n"
"\n"
"// Head returns the first item in xs.\n"
"//\n"
"// Panics if xs is empty.\n"
"fn head xs = match xs\n"
"| Empty    = panic(\"list is empty\")\n"
"| Cons x _ = x\n"
";\n"
"\n"
"// Tail returns all of the items after the first item in xs.\n"
"//\n"
"// If xs is empty or has length 1, the empty list is returned.\n"
"fn tail xs = match xs\n"
"| Empty     = xs\n"
"| Cons _ ys = ys\n"
";\n"
"\n"
"// Reverse reverses a list.\n"
"fn reverse xs {\n"
"let rs = Empty()\n"
"while xs != Empty() {\n"
"rs = Cons(head(xs), rs)\n"
"xs = tail(xs)\n"
"}\n"
"return rs\n"
"}\n"
"\n"
"// Concat appends the list ys to the list xs.\n"
"fn concat xs ys  {\n"
"xs = reverse(xs)\n"
"while xs != Empty() {\n"
"ys = Cons(head(xs), ys)\n"
"xs = tail(xs)\n"
"}\n"
"return ys\n"
"}\n"
"\n"
"// Format formats xs as a string 'x1,x2,...,xN'.\n"
"fn format xs = _format(xs, \"\")\n"
"fn _format xs string {\n"
"if xs == Empty() return string\n"
"\n"
"let x = head(xs)\n"
"xs = tail(xs)\n"
"\n"
"if xs == Empty() return string + \"%(x)\"\n"
"return _format(xs, string + \"%(x),\")\n"
"}\n"
"\n"
"// Quicksort sorts a list using the quicksort algorithm.\n"
"fn quicksort xs {\n"
"if length(xs) == 0 {\n"
"return xs\n"
"}\n"
"\n"
"let x = head(xs)\n"
"let rest = tail(xs)\n"
"\n"
"fn lte v = v <= x\n"
"fn gte v = v >= x\n"
"\n"
"let lesser = quicksort(where(lte, rest))\n"
"let greater = quicksort(where(gte, rest))\n"
"let middle = Cons(x, Empty())\n"
"\n"
"return concat(lesser, concat(middle, greater))\n"
"}\n"
"\n"
"// Sort sorts a list.\n"
"fn sort xs = quicksort(xs)\n"
"\n";