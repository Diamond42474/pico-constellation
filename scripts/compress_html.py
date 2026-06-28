#!/usr/bin/env python3
import argparse
import re
from pathlib import Path


def compress_html(text: str) -> str:
    # Remove HTML comments
    text = re.sub(r"<!--.*?-->", "", text, flags=re.DOTALL)

    # Collapse whitespace between tags and text nodes, but preserve spaces in pre/code if present
    text = re.sub(r">\s+", ">", text)
    text = re.sub(r"\s+<", "<", text)

    # Remove whitespace around common punctuation that doesn't need it
    text = re.sub(r"\s+([<>/=])", r"\1", text)
    text = re.sub(r"([<>/=])\s+", r"\1", text)

    # Collapse multiple spaces/tabs/newlines to a single space outside of specific tags
    text = re.sub(r"[ \t\r\n]+", " ", text)

    # Trim leading/trailing whitespace
    text = text.strip()

    return text


def main() -> None:
    parser = argparse.ArgumentParser(description="Minify an HTML file and save it as name_compressed.html")
    parser.add_argument("input", type=Path, help="Path to the input HTML file")
    parser.add_argument("-o", "--output", type=Path, help="Optional output path")
    args = parser.parse_args()

    input_file = args.input
    if not input_file.exists():
        raise FileNotFoundError(f"Input file not found: {input_file}")

    if args.output is not None:
        output_file = args.output
    else:
        output_file = input_file.with_name(f"{input_file.stem}_compressed{input_file.suffix}")

    html = input_file.read_text(encoding="utf-8")
    compressed = compress_html(html)
    output_file.write_text(compressed, encoding="utf-8")
    print(f"Wrote {output_file}")


if __name__ == "__main__":
    main()
