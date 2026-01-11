#!/bin/bash
set -e

# Paths
REPO_ROOT="/Users/michel/MichelFile/pappllocal/raw_github/PAPPL"
DATASETS_ROOT="/Users/michel/MichelFile/pappllocal/raw_github/PAPPL_datasets"
USB_ROOT="/Volumes/76021338/demo_sncf"
MAIN_PROGRAM="$REPO_ROOT/build/MainProgram"

# Reference Files
REF_NAME="2025-11-13_11-46-43_ref"
REF_JSON="$USB_ROOT/$REF_NAME/build/OS-1-128_992425000327_1024x10_20251112_180355.json"
REF_PCAP="$USB_ROOT/$REF_NAME/build/OS-1-128_992425000327_1024x10_20251112_180355.pcap"
# Use the VERIFIED targets (Z ~ 0.25m) which matches the PCAP data.
# This ensures filters work correctly without needing python pre-alignment.
TARGETS_CSV="$REPO_ROOT/verified_targets.csv"

echo "Using Reference: $REF_NAME"

# Function to run one comparison
run_comparison() {
    NAME=$1
    PCAP_FILE=$2
    echo "---------------------------------------------------"
    echo "Processing candidate: $NAME"
    
    CANDIDATE_PCAP="$USB_ROOT/$NAME/build/$PCAP_FILE"
    OUTPUT_CSV="$REPO_ROOT/output_${NAME}.csv"
    
    if [ ! -f "$CANDIDATE_PCAP" ]; then
        echo "Warning: Candidate PCAP not found: $CANDIDATE_PCAP"
        return
    fi
     if [ ! -f "$TARGETS_CSV" ]; then
        echo "Warning: Targets CSV not found: $TARGETS_CSV"
        return
    fi
    
    echo "Running MainProgram with high-fidelity inputs..."
    # Config:
    # --grid-res-m=0.02 (2cm) for fine detail (High Fidelity)
    # --max-scans=5 to minimize drift
    # --max-match-m=5.0 and --fallback-m=10.0 to handle large shifts (V2 Logic)
    # Using verified_targets.csv ensures filters initialize in valid data region.
    "$MAIN_PROGRAM" \
        "$REF_JSON" \
        "$TARGETS_CSV" \
        "$OUTPUT_CSV" \
        "$REF_PCAP" \
        "$CANDIDATE_PCAP" \
        --grid-res-m=0.02 \
        --max-match-m=5.0 \
        --fallback-m=10.0 \
        --max-scans=5
        
    echo "Generated: $OUTPUT_CSV"
}

# 1. 5mm Lift
run_comparison "2025-11-13_12-23-15_5mm_6.5"    "OS-1-128_992425000327_1024x10_20251112_184028.pcap"

# 2. Return to Normal 1 (ini1)
run_comparison "2025-11-13_14-12-41_ini1"       "OS-1-128_992425000327_1024x10_20251112_185729.pcap"

# 3. 20mm Lift (using 14-43-44)
run_comparison "2025-11-13_14-43-44_20mm_9"     "OS-1-128_992425000327_1024x10_20251112_190723.pcap"

# 4. Return to Normal 2 (ini2)
run_comparison "2025-11-13_14-52-38_ini2"       "OS-1-128_992425000327_1024x10_20251112_191615.pcap"

echo "---------------------------------------------------"
echo "All processing complete."
