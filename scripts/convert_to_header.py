import numpy as np
import argparse
from pathlib import Path

def convert_raw_to_header(input_file, output_file, array_name="audio_data"):
    # Read raw file as uint16
    data = np.fromfile(input_file, dtype=np.uint16)

    # Format as C array (hex is easier to read/debug)
    lines = []
    line = []
    for i, val in enumerate(data):
        line.append(f"0x{val:04X}")
        if len(line) == 12:  # values per line
            lines.append(", ".join(line))
            line = []
    if line:
        lines.append(", ".join(line))

    array_body = ",\n    ".join(lines)

    header = f"""#ifndef {array_name.upper()}_H
#define {array_name.upper()}_H

#include <stdint.h>

#define {array_name.upper()}_LEN {len(data)}

const uint16_t {array_name}[{len(data)}] = {{
    {array_body}
}};

#endif // {array_name.upper()}_H
"""

    Path(output_file).write_text(header)
    print(f"Wrote {len(data)} samples to {output_file}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert .raw to C header uint16_t array")
    parser.add_argument("input", help="Input .raw file")
    parser.add_argument("output", help="Output .h file")
    parser.add_argument("--name", default="audio_data", help="C array name")

    args = parser.parse_args()

    convert_raw_to_header(args.input, args.output, args.name)