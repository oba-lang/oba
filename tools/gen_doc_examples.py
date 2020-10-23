import argparse
import glob
import os
import re
import sys

from subprocess import PIPE, Popen

EXAMPLE_DIR = "test/examples"

BEGIN_EXAMPLE_RE = re.compile("// example: ([\w\/]+) (.*)")
END_EXAMPLE_RE = re.compile("// end example")


class GenerateError(Exception):
    def __init__(self, message):
        self.message = message


def get_args():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--content",
        help="The path to the generated documentation's 'contents' dir",
        default="./docs/content",
    )
    parser.add_argument(
        "--glob",
        help="Filter example files by name",
        default="**/*.oba",
    )
    return parser.parse_args()


def scrape_examples(example_file):
    examples = []
    example = None

    # Parse the test expectations.
    with open(example_file, "r") as f:
        for line in f.readlines():
            if line.strip() == "" and not example:
                continue

            match = BEGIN_EXAMPLE_RE.search(line)
            if match:
                example = {
                    "guide": match.group(1),
                    "name": match.group(2),
                    "code": [],
                }
                continue

            match = END_EXAMPLE_RE.search(line)
            if match:
                if not example:
                    raise GenerateError("not in an example")
                examples.append(example)
                example = None
                continue

            if example:
                example["code"].append(line)

    return examples


def generate(example_files, content_dir):
    examples = []
    for example_file in example_files:
        examples.extend(scrape_examples(example_file))

    guides = set(())
    for example in examples:
        guides.add(example["guide"])

    for guide in guides:
        guide_path = "{}/{}.md".format(content_dir, guide)
        with open(guide_path) as f:
            new_lines = f.readlines()

        for example in examples:
            for i in range(len(new_lines)):
                line = new_lines[i]
                if line.strip() == ("<!-- example %s -->" % example["name"]):
                    snippet = ["\n```\n"]
                    snippet.extend(example["code"])
                    snippet += ["```\n"]
                    new_lines = new_lines[: i - 1] + snippet + new_lines[i + 1 :]

        with open(guide_path, "w") as f:
            print("".join(new_lines))
            f.write("".join(new_lines))


def main():
    args = get_args()
    example_file_glob = os.path.join(EXAMPLE_DIR, args.glob)
    example_files = glob.glob(example_file_glob, recursive=True)
    sys.exit(generate(example_files, args.content))


main()
