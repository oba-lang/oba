// Generated automatically from mod/option.oba. Do not edit.
const char* optionModSource =

"// Option represents a possible value.\n"
"//\n"
"// This is a safe alternative to returning null values.\n"
"// * None represents the absence of a value.\n"
"// * Some represents the presence of a value.\n"
"data Option = None | Some value\n"
"\n"
"// Must returns the value held in the given option, if it is an Option.\n"
"//\n"
"// If it is not an option, or it is None, a fatal error is raised.\n"
"fn must option = match option\n"
"  | None   = panic(\"expected a value\")\n"
"  | Some v = v\n"
"  ;\n";