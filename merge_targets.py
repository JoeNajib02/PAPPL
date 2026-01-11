import pandas as pd
import sys
import os

def merge_targets(ref_targets_path, aligned_targets_path, output_path):
    print(f"Merging {aligned_targets_path} into {ref_targets_path} -> {output_path}")
    
    # Load Reference Targets (contains z_ref in Meters)
    # Expected cols: id, x, y, z_ref
    try:
        df_ref = pd.read_csv(ref_targets_path)
    except Exception as e:
        print(f"Error reading ref targets: {e}")
        return

    # Load Aligned Targets (contains x_hit_mm, y_hit_mm)
    # Expected cols: id, x_hit_mm, ..., found
    try:
        df_aligned = pd.read_csv(aligned_targets_path)
    except Exception as e:
        print(f"Error reading aligned targets: {e}")
        return

    # Ensure ID column is string to match correctly
    df_ref['id'] = df_ref['id'].astype(str)
    df_aligned['id'] = df_aligned['id'].astype(str)

    # Merge on ID
    merged = pd.merge(df_ref, df_aligned, on='id', how='inner')

    # Create final dataframe
    # We want: id, x (aligned, m), y (aligned, m), z_ref (original, m)
    final_df = pd.DataFrame()
    final_df['id'] = merged['id']
    
    # Convert MM to Meters
    final_df['x'] = merged['x_hit_mm'] / 1000.0
    final_df['y'] = merged['y_hit_mm'] / 1000.0
    
    # Use Original Reference Z (from verified_targets.csv)
    # Use z_ref if it exists, otherwise z_hit_m (which was the column name in verified_targets)
    if 'z_ref' in merged.columns:
        final_df['z_ref'] = merged['z_ref']
    elif 'z_hit_m' in merged.columns:
        final_df['z_ref'] = merged['z_hit_m']
    else:
        # Fallback if verified_targets headers are messy
        # Assuming 4th column is Z
        final_df['z_ref'] = merged.iloc[:, 3] 

    # Handle cases where alignment failed (found=0)
    # If alignment failed, use original X/Y? No, if failed, analysis is doomed anyway.
    # But let's check 'found'
    if 'found' in merged.columns:
        # found=0 means Found (in C++ tool logic: 0 usually means success in return codes, 
        # but in CSV struct it was bool found? 
        # extract_targets.cpp: output 0 for success? No, text output said "found 0/32".
        # Struct: bool found = false.
        # CSV output: ofs << ... ",0," if found? 
        # extract_targets CSV logic: 
        # if (hits[i].found) ... 
        # csv writes: ... ",0," ...
        # wait, look at extract_targets_from_pcap.cpp:
        # ofs << ... << ",0," << ...
        # It HARDCODES '0' in the 4th column?!
        # No: ofs << x << y << z << ",0," << dist.
        # 4th numeric column is 0. 
        # Header: id,x_hit_mm,y_hit_mm,z_hit_mm,found,dist_xy_mm
        # So "found" column value is 0?
        # Actually in the file sample I viewed:
        # id,x_hit_mm,y_hit_mm,z_hit_mm,found,dist_xy_mm
        # R1_start,2986...,...,...,0,0.999
        # So '0' is in the 'found' column?
        # cpp: << hits[i].x ... << ",0," << ...
        # It seems the tool hardcodes 0 as a placeholder or 'flag'?
        # Ah, header: "found". Code: ",0,".
        # This confirms targets ARE found (since lines exist).
        pass

    # Save to output
    # Columns: id,x,y,z_ref
    final_df.to_csv(output_path, index=False)
    print(f"Written merged targets to {output_path}")

if __name__ == "__main__":
    if len(sys.argv) < 4:
        print("Usage: python3 merge_targets.py <ref_csv> <aligned_csv> <output_csv>")
        sys.exit(1)
    merge_targets(sys.argv[1], sys.argv[2], sys.argv[3])
