@echo off
REM Template for running compare_point_repeatability.exe with generated rail targets.
set "EXE=.\build-win\Release\compare_point_repeatability.exe"

REM Update these paths to match your environment.
set "METADATA_JSON=path\to\metadata.json"
set "TARGETS_CSV=examples\targets_rails_10pts.csv"
set "OUT_CSV=rail_results_10pts.csv"
set "REFP_CAP=path\to\ref.pcap"
set "LIFT5_PCAP=path\to\lift_5mm.pcap"
set "BACK1_PCAP=path\to\back1.pcap"
set "LIFT10_PCAP=path\to\lift_10mm.pcap"
set "BACK2_PCAP=path\to\back2.pcap"
set "LIFT20_PCAP=path\to\lift_20mm.pcap"

REM The first PCAP is the reference.
REM The remaining PCAPs are the lift/return acquisitions:
REM   5 mm lift, back to nominal, 10 mm lift, back to nominal, 20 mm lift.
REM Edit the variables above to point to your actual file paths.
"%EXE%" ^
  "%METADATA_JSON%" ^
  "%TARGETS_CSV%" ^
  "%OUT_CSV%" ^
  "%REFP_CAP%" ^
  "%LIFT5_PCAP%" ^
  "%BACK1_PCAP%" ^
  "%LIFT10_PCAP%" ^
  "%BACK2_PCAP%" ^
  "%LIFT20_PCAP%" ^
  --grid-res-m=0.02 ^
  --max-scans=5
