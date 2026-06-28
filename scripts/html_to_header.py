#!/usr/bin/env python3
import argparse
import re
from pathlib import Path


def sanitize_identifier(name: str) -> str:
    identifier = re.sub(r"[^0-9A-Za-z_]", "_", name)
    if not identifier:
        identifier = "html_data"
    if identifier[0].isdigit():
        identifier = "_" + identifier
    return identifier


def escape_c_string(text: str) -> str:
    escaped = []
    for ch in text:
        if ch == "\\":
            escaped.append("\\\\")
        elif ch == '"':
            escaped.append('\\"')
        elif ch == "\n":
            escaped.append("\\n")
        elif ch == "\r":
            escaped.append("\\r")
        elif ch == "\t":
            escaped.append("\\t")
        else:
            escaped.append(ch)
    return "".join(escaped)


def build_string_literals(text: str) -> list[str]:
    lines = text.splitlines(keepends=True)
    literals: list[str] = []

    for line in lines:
        newline = line.endswith("\n")
        content = line[:-1] if newline else line
        escaped = escape_c_string(content)
        if newline:
            escaped += "\\n"

        literals.append(f'"{escaped}"')

    return literals or ['""']


def convert_html_to_header(input_file: Path, output_file: Path, var_name: str | None = None, guard_name: str | None = None) -> None:
    html_content = input_file.read_text(encoding="utf-8")
    if var_name is None:
        var_name = sanitize_identifier(input_file.stem)
    if guard_name is None:
        guard_name = f"{sanitize_identifier(input_file.stem.upper())}_H"

    literal_parts = build_string_literals(html_content)
    literal_block = "\n".join(f"    {part}" for part in literal_parts)

    header = f"""#ifndef {guard_name}
#define {guard_name}

static const char {var_name}[] =
{literal_block};

#endif /* {guard_name} */
"""

    output_file.write_text(header, encoding="utf-8")
    print(f"Wrote {input_file} -> {output_file}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert an HTML file into a C header containing the HTML as a string literal")
    parser.add_argument("input", type=Path, help="Path to the input .html file")
    parser.add_argument("output", type=Path, nargs="?", help="Path to the output .h file (defaults to input stem + .h)")
    parser.add_argument("--name", dest="var_name", help="Name of the generated C array/variable")
    parser.add_argument("--guard", dest="guard_name", help="Include guard name")
    args = parser.parse_args()

    input_file = args.input
    output_file = args.output or input_file.with_suffix(".h")

    convert_html_to_header(input_file, output_file, var_name=args.var_name, guard_name=args.guard_name)
