#!/usr/bin/env bash
# Record the machine state that affects benchmark results (stored next to every result set).
# Reads only world-readable files; never needs root. Output: "key: value" lines.
r() { [[ -r "$1" ]] && tr -d '\n' < "$1" || echo "n/a"; }
echo "date: $(date -Iseconds)"
echo "host_kernel: $(uname -srm)"
echo "cpu_model: $(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2- | sed 's/^ //')"
echo "logical_cpus: $(nproc --all)"
echo "p_core_cpus: $(r /sys/devices/cpu_core/cpus)"
echo "e_core_cpus: $(r /sys/devices/cpu_atom/cpus)"
echo "memory_total_mb: $(awk '/MemTotal/{print int($2/1024)}' /proc/meminfo)"
echo "memory_available_mb: $(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo)"
echo "compiler: $(${CC:-gcc} --version | head -1)"
echo "governor: $(r /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
echo "energy_perf_preference: $(r /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference)"
echo "platform_profile: $(r /sys/firmware/acpi/platform_profile)"
echo "no_turbo: $(r /sys/devices/system/cpu/intel_pstate/no_turbo)"
for z in intel-rapl:0 intel-rapl-mmio:0; do
  d=/sys/class/powercap/$z
  [[ -d $d ]] || continue
  echo "power_limit_${z}_long_term_w: $(awk '{printf "%.1f", $1/1e6}' $d/constraint_0_power_limit_uw 2>/dev/null)"
  echo "power_limit_${z}_long_term_window_s: $(awk '{printf "%.1f", $1/1e6}' $d/constraint_0_time_window_us 2>/dev/null)"
  echo "power_limit_${z}_short_term_w: $(awk '{printf "%.1f", $1/1e6}' $d/constraint_1_power_limit_uw 2>/dev/null)"
done
echo "energy_counter_readable: $([[ -r /sys/class/powercap/intel-rapl:0/energy_uj ]] && echo yes || echo no)"
echo "perf_event_paranoid: $(r /proc/sys/kernel/perf_event_paranoid)"
for c in /sys/devices/system/cpu/cpu[0-9]*; do
  n=${c##*cpu}; printf "cpu%s_max_mhz: %s\n" "$n" "$(awk '{print int($1/1000)}' $c/cpufreq/cpuinfo_max_freq 2>/dev/null)"
done
for z in /sys/class/thermal/thermal_zone*; do
  echo "temp_$(r $z/type)_c: $(awk '{printf "%.1f", $1/1000}' $z/temp 2>/dev/null)"
done
echo "on_ac_power: $(cat /sys/class/power_supply/A*/online 2>/dev/null | head -1 || echo n/a)"
echo "load_average: $(cut -d' ' -f1-3 /proc/loadavg)"
