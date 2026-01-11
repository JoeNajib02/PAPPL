import pandas as pd
import matplotlib.pyplot as plt
import glob
import os

def plot_csv(csv_path):
    print(f"Plotting {csv_path}...")
    try:
        df = pd.read_csv(csv_path)
    except Exception as e:
        print(f"Failed to read {csv_path}: {e}")
        return

    # Filter out empty rows or purely NaN rows if necessary
    # df = df.dropna(how='all')

    plt.figure(figsize=(12, 6))
    
    # Plot Raw
    if 'dz_raw_m' in df.columns:
        plt.plot(df['point_id'], df['dz_raw_m'] * 1000, label='Raw (mm)', marker='o', linestyle='--', alpha=0.5)

    # Plot Filters
    # We look for columns starting with dz_filt_ and ending with _m
    filter_cols = [c for c in df.columns if c.startswith('dz_filt_') and c.endswith('_m') and 'best' not in c]
    
    for col in filter_cols:
        label = col.replace('dz_filt_', '').replace('_m', '')
        plt.plot(df['point_id'], df[col] * 1000, label=label, marker='x')

    plt.axhline(0, color='black', linewidth=1)
    plt.xlabel('Point ID')
    plt.ylabel('Deviation from Reference (mm)')
    plt.title(f'Repeatability Analysis: {os.path.basename(csv_path)}')
    plt.legend()
    plt.grid(True)
    plt.xticks(rotation=45)
    plt.tight_layout()
    
    out_path = csv_path.replace('.csv', '.png')
    plt.savefig(out_path)
    print(f"Saved {out_path}")
    plt.close()

files = glob.glob("output_*.csv")
for f in files:
    if "_filtered.csv" in f:
        continue
    # Skip the generic output_5mm.csv if output_...5mm_6.5.csv exists (duplicates)
    # But for now just plot everything.
    plot_csv(f)
