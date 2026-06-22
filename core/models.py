from dataclasses import dataclass

@dataclass(frozen=True)
class TelemetryData:
        cpu_usage: float = 0.0
        cpu_cores: tuple = ()
        cpu_freq: float = 0.0
        cpu_temp: float | None = None 
        cpu_name:str = "Unknow CPU"

        gpu_usage:float = 0.0
        gpu_temp:float | None = None
        gpu_memoryTot:float = 0.0
        gpu_c_memory:float = 0.0
        gpu_c_memory_perc:float = 0.0
        gpu_name:str = "Unknow GPU"

        ram_usage: float = 0.0
        ram_total:float = 0.0


        os_name:str = ""

