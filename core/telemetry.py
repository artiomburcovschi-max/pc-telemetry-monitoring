import sys
import os
import importlib
import platform
import psutil
import GPUtil
from PySide6.QtCore import QThread,Signal
from cpuinfo import get_cpu_info
#sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))) 
from core.models import TelemetryData

class TelemetryThread(QThread):
    my_signal = Signal(TelemetryData)
    
    def __init__(self):
        super().__init__()
        self.is_running = True
        self.os_current = platform.system()
        self.os_name_display = self._get_os_info()
        
        try:
            info = get_cpu_info()
            self.current_cpu_name = info.get('brand_raw', 'Unknown CPU')
        except Exception:
            self.current_cpu_name = 'Unknown CPU'
        try:
            gpus = GPUtil.getGPUs()
            if gpus:
                try:
                    self.current_gpu_name = gpus[0].name
                except Exception:
                    self.current_gpu_name = 'No Nvidia GPU'
                try:
                    self.current_vram_total = round(gpus[0].memoryTotal / 1024, 1) 
                except Exception:
                    self.current_vram_total = 0.0
            else:
                self.current_gpu_name = 'Unknown GPU'
                self.current_vram_total = 0.0
        except Exception:
            self.current_gpu_name = 'Unknown GPU'
            self.current_vram_total = 0.0
        try:
            self.current_totram = psutil.virtual_memory().total / (1024**3)
        except Exception:
            self.current_totram = 0.0
    
    def _get_os_info(self) -> str:
        if self.os_current == 'Linux':
            try:
                info = platform.freedesktop_os_release()
                return info.get("PRETTY_NAME", f"Linux {platform.release()}")
            except (AttributeError, OSError):
                return f"Linux {platform.release()}"
                
        elif self.os_current == 'Windows':
            try:
                return f"Windows {platform.release()} (Build {platform.version()})"
            except Exception:
                return "Unknown Windows"
                
        return self.os_current

    def _get_cpu_temperature(self) -> float | None:
        if self.os_current == 'Linux':
            try:
                temps = psutil.sensors_temperatures()
                if not temps:
                    return None
                for key in ['coretemp', 'k10temp']:
                    if key in temps:
                        for sensor in temps[key]:
                            if sensor.label in ['Package id 0', 'Tctl', 'Tdie']:
                                return sensor.current
                for key,sensors in temps.items():
                    if sensors:
                        return sensors[0].current
            except Exception:
                return None
        if self.os_current == 'Windows':
            try:
                win_module = importlib.import_module('WinTmp')
                if win_module:
                    c_temp = win_module.CPU_Temp() 
                    return c_temp
            except Exception:
                return None
        return None

    def run(self):
        psutil.cpu_percent(interval=None)
        
        while self.is_running:        

            current_cores = ()
            current_cpu = 0.0
            current_freq = 0.0
            current_ram = 0.0
            current_gpu = 0.0
            current_gputemp = None
            current_vram_percent = 0.0
            current_vram_used = 0.0
            try:
                try:
                    current_cores = tuple(psutil.cpu_percent(interval=None, percpu=True))
                    current_cpu = sum(current_cores) / len(current_cores)
                    filtred_cores = [
                        (idx+1,load)for idx,load in enumerate(current_cores) if load > 1
                    ]
                    current_cores = tuple(filtred_cores)
                except Exception:
                    current_cores = ()
                    current_cpu = 0.0
                try:
                    current_freq = psutil.cpu_freq().current
                except Exception:
                    current_freq = 0.0
                try:
                    current_ram = psutil.virtual_memory().percent
                except Exception:
                    current_ram = 0.0

                current_disks_list = []
                partitions = psutil.disk_partitions(all=False)
                for part in partitions:
                    try:
                        usage = psutil.disk_usage(part.mountpoint)
                        disk_data = {
                            "name":part.mountpoint,
                            "total":round(usage.total / (1024**3)),
                            "Usage":round(usage.used / (1024**3)),
                            "free":round(usage.free / (1024**3)),
                            "Percent":round(usage.percent)
                            }
                        current_disks_list.append(disk_data)
                    except Exception:
                        continue
                try:
                    gpus = GPUtil.getGPUs()
                    if gpus:
                        current_gpu = round(gpus[0].load * 100, 1)
                        current_gputemp = gpus[0].temperature
                       
                        current_vram_percent = round(gpus[0].memoryUtil * 100, 1)      
                        current_vram_used = round(gpus[0].memoryUsed / 1024, 2)         
                except Exception:
                    current_gpu = 0.0
                    current_gputemp = None
                    current_vram_percent = 0.0
                    current_vram_used = 0.0
                current_cputemp = self._get_cpu_temperature()
                data = TelemetryData(
                    cpu_usage = current_cpu,
                    cpu_cores = current_cores,
                    cpu_freq = current_freq,
                    cpu_temp = current_cputemp,
                    ram_usage = current_ram,
                    ram_total = self.current_totram,
                    gpu_usage = current_gpu,
                    gpu_temp = current_gputemp,
                    gpu_name = self.current_gpu_name,
                    gpu_memoryTot = self.current_vram_total,
                    gpu_c_memory = current_vram_used,
                    gpu_c_memory_perc = current_vram_percent,
                    cpu_name = self.current_cpu_name,
                    os_name = self.os_name_display,
                    disk_info = current_disks_list
                )
                self.my_signal.emit(data)  
            except Exception as e:
                print(e)
            finally:
                self.msleep(1000)
    def stop(self):
        self.is_running = False
        self.quit()
        self.wait()
