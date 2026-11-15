import time
import subprocess
import serial
import psutil

try:
    import wmi
except ImportError:
    wmi = None

# --- CONFIGURATION ---
SERIAL_PORT = 'COM3'   # Replace with your ESP32 COM port (e.g., 'COM3', 'COM4')
BAUD_RATE   = 115200

def get_nvidia_stats():
    """Fetches real-time GPU temperature and usage directly via NVIDIA SMI."""
    try:
        # Runs hidden without popping up a console window on Windows
        startupinfo = subprocess.STARTUPINFO()
        startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        output = subprocess.check_output(
            ['nvidia-smi', '--query-gpu=temperature.gpu,utilization.gpu', '--format=csv,noheader,nounits'],
            encoding='utf-8',
            startupinfo=startupinfo,
            timeout=1
        )
        temp_str, usage_str = output.strip().split(',')
        return int(temp_str.strip()), int(usage_str.strip())
    except Exception:
        return None, None

def get_hardware_monitor_stats(wmi_ohm):
    """Reads real-time CPU/GPU sensors from OpenHardwareMonitor or LibreHardwareMonitor WMI."""
    cpu_t, gpu_t, gpu_u = None, None, None
    if not wmi_ohm:
        return cpu_t, gpu_t, gpu_u
    try:
        for sensor in wmi_ohm.Sensor():
            name = sensor.Name.lower()
            stype = sensor.SensorType
            val = int(float(sensor.Value))
            
            if stype == 'Temperature':
                if any(k in name for k in ('cpu package', 'cpu total', 'tctl', 'tdie', 'core average')):
                    cpu_t = val
                elif 'gpu' in name and gpu_t is None:
                    gpu_t = val
            elif stype == 'Load':
                if 'gpu core' in name:
                    gpu_u = val
    except Exception:
        pass
    return cpu_t, gpu_t, gpu_u

def get_acpi_cpu_temp(wmi_acpi):
    """Fallback: Reads motherboard ACPI thermal zone temperature via standard Windows WMI."""
    if not wmi_acpi:
        return None
    try:
        for temp in wmi_acpi.MSAcpi_ThermalZoneTemperature():
            kelvin = temp.CurrentTemperature / 10.0
            celsius = int(kelvin - 273.15)
            if 15 < celsius < 115:
                return celsius
    except Exception:
        pass
    return None

def main():
    # Wait briefly on system startup before opening Serial port
    time.sleep(3)

    wmi_ohm = None
    wmi_acpi = None

    if wmi:
        # Connect to LibreHardwareMonitor or OpenHardwareMonitor WMI namespace if active
        for namespace in ("root\\LibreHardwareMonitor", "root\\OpenHardwareMonitor"):
            try:
                temp_wmi = wmi.WMI(namespace=namespace)
                if temp_wmi.Sensor():
                    wmi_ohm = temp_wmi
                    print(f"Connected to sensor namespace: {namespace}")
                    break
            except Exception:
                pass
        
        try:
            wmi_acpi = wmi.WMI(namespace="root\\WMI")
        except Exception:
            pass

    while True:
        try:
            with serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1) as esp32:
                print(f"Connected to ESP32 on {SERIAL_PORT}. Streaming real-time PC stats...")
                time.sleep(2)

                while True:
                    # 1. Real-time CPU and RAM usage via psutil
                    cpu_usage = int(psutil.cpu_percent(interval=0.5))
                    ram_usage = int(psutil.virtual_memory().percent)

                    # 2. Query Hardware Monitor WMI (if running)
                    cpu_temp, gpu_temp, gpu_usage = get_hardware_monitor_stats(wmi_ohm)

                    # 3. Query NVIDIA GPU directly via nvidia-smi if not found in WMI
                    if gpu_temp is None or gpu_usage is None:
                        nv_temp, nv_usage = get_nvidia_stats()
                        if gpu_temp is None:
                            gpu_temp = nv_temp
                        if gpu_usage is None:
                            gpu_usage = nv_usage

                    # 4. Fallback for CPU temperature via Windows ACPI
                    if cpu_temp is None:
                        cpu_temp = get_acpi_cpu_temp(wmi_acpi)

                    # Safe fallbacks if a sensor is unavailable without admin privileges
                    cpu_temp  = cpu_temp if cpu_temp is not None else 45
                    gpu_temp  = gpu_temp if gpu_temp is not None else 50
                    gpu_usage = gpu_usage if gpu_usage is not None else 0

                    # Format expected by ESP32: C:<cpu_t>,G:<gpu_t>,U:<cpu_u>,P:<gpu_u>,M:<ram_u>\n
                    payload = f"C:{cpu_temp},G:{gpu_temp},U:{cpu_usage},P:{gpu_usage},M:{ram_usage}\n"
                    esp32.write(payload.encode('utf-8'))

                    time.sleep(0.5)

        except Exception as e:
            print(f"Connection lost or port busy ({e}). Retrying in 5 seconds...")
            time.sleep(5)

if __name__ == "__main__":
    main()
