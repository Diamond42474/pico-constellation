import pandas as pd
import matplotlib.pyplot as plt

# Load CSV
df = pd.read_csv("output.csv")

# Plot everything
plt.figure(figsize=(12, 8))

#plt.plot(df["y1200"], label="y1200")
plt.plot(df["y2200"], label="y2200")
plt.plot(df["metric"], label="metric")

plt.legend()
plt.title("Bandpass Outputs and Metric")
plt.xlabel("Sample")
plt.ylabel("Amplitude")

plt.tight_layout()
plt.savefig("output_csv.png", dpi=150)
print("Saved to output_csv.png")
