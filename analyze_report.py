import pandas as pd
import numpy as np
import sys
import glob

def analyze_file(filepath):
    print(f"\n{'='*60}")
    print(f"ANALYSIS: {filepath.split('/')[-1]}")
    print(f"{'='*60}")
    
    try:
        df = pd.read_csv(filepath)
    except Exception as e:
        print(f"Error reading file: {e}")
        return

    # Columns of interest
    filter_cols = [c for c in df.columns if c.startswith('dz_filt_') and c.endswith('_m') and 'best' not in c]
    raw_col = 'dz_raw_m'
    
    if raw_col not in df.columns:
        print("Error: 'dz_raw_m' column not found.")
        return

    # Calculate Raw Stats
    raw_mean = df[raw_col].mean() * 1000 # convert to mm
    raw_std = df[raw_col].std() * 1000
    
    print(f"{'Filter':<20} | {'Mean (mm)':<10} | {'StdDev (mm)':<12} | {'Noise Red. (%)':<15}")
    print(f"{'-'*65}")
    print(f"{'RAW (Baseline)':<20} | {raw_mean:10.2f} | {raw_std:12.2f} | {'-':<15}")
    
    best_filter = "None"
    lowest_std = raw_std
    
    for col in filter_cols:
        filter_name = col.replace('dz_filt_', '').replace('_m', '')
        
        # Stats
        f_mean = df[col].mean() * 1000
        f_std = df[col].std() * 1000
        
        # Improvement calculation (reduction in StdDev = reduction in noise)
        improvement = ((raw_std - f_std) / raw_std) * 100.0 if raw_std != 0 else 0.0
        
        print(f"{filter_name:<20} | {f_mean:10.2f} | {f_std:12.2f} | {improvement:14.1f}%")
        
        if f_std < lowest_std:
            lowest_std = f_std
            best_filter = filter_name

    print(f"{'-'*65}")
    print(f"🏆 BEST FILTER (Most Stable): {best_filter.upper()}")
    
    # Interpretation
    print(f"\n🔍 INTERPRETATION:")
    if abs(raw_mean) < 1.0:
         print(f"  • Physical Shift: The data shows almost NO physical movement (Mean ~ {raw_mean:.1f}mm).")
    else:
         print(f"  • Physical Shift: The data CONFIRMS a physical movement of approx {raw_mean:.1f} mm.")
         
    if lowest_std < 1.0:
        print(f"  • Quality: Excellent! The {best_filter} filter reduced noise to sub-millimeter level ({lowest_std:.2f}mm).")
    elif lowest_std < 5.0:
         print(f"  • Quality: Good. Noise is reasonable ({lowest_std:.2f}mm).")
    else:
         print(f"  • Quality: Poor. There is still significant variation ({lowest_std:.2f}mm).")

if __name__ == "__main__":
    files = glob.glob("output_*.csv")
    files.sort()
    for f in files:
        if "filtered" in f: continue # Skip intermediate files
        analyze_file(f)
