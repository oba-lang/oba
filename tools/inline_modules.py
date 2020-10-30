"""Generates C code to inline core modules into the interpreter"""

import glob
import os
import sys


def transform_source_code(filename, lines):
    mod_name = os.path.basename(filename)[:-4]  # strip .oba suffix
    output = [
        "// Generated automatically from %s. Do not edit." % filename,
        "const char* %sModSource =" % mod_name,
        "",
    ] + ['"%s\\n"' % line.rstrip() for line in lines]
    output[-1] += ";"
    return output


def inline_file(filename):
    with open(filename, "r") as f:
        new_lines = transform_source_code(filename, f.readlines())

    outfile = filename + ".c"
    with open(outfile, "w") as f:
        f.write("\n".join(new_lines))


def inline_files(filepaths):
    for filepath in filepaths:
        inline_file(filepath)
    return 0


def main():
    mod_files_glob = os.path.join("mod", "*.oba")
    mod_files = glob.glob(mod_files_glob)
    sys.exit(inline_files(mod_files))


main()
