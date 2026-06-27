import subprocess
import sys

FLASH_TOTAL = 4 * 1024 * 1024
RAM_TOTAL   = 512 * 1024

elf = sys.argv[1]

out = subprocess.check_output(
    ["arm-none-eabi-size", "-B", elf],
    text=True
)

lines = out.strip().splitlines()[1].split()

text = int(lines[0])
data = int(lines[1])
bss  = int(lines[2])

flash_used = text + data
ram_used = data + bss

print("=== Memory Usage ===")
print(f"FLASH: {flash_used}/{FLASH_TOTAL} ({flash_used/FLASH_TOTAL*100:.2f}%)")
print(f"RAM:   {ram_used}/{RAM_TOTAL} ({ram_used/RAM_TOTAL*100:.2f}%)")