from dataclasses import dataclass,field

@dataclass(frozen=True)
class TelemetryData:
        cpu_usage: float = 0.0
        cpu_cores: tuple = ()
        cpu_freq: float = 0.0
        cpu_temp: float | None = None 
        cpu_name:str = "Unknown CPU"

        gpu_usage:float = 0.0
        gpu_temp:float | None = None
        gpu_memoryTot:float = 0.0
        gpu_c_memory:float = 0.0
        gpu_c_memory_perc:float = 0.0
        gpu_name:str = "Unknown GPU"

        ram_usage: float = 0.0
        ram_total:float = 0.0

        disk_info: list = field(default_factory=list)
        
        os_name:str = ""

        net_download_speed: float = 0.0
        net_upload_speed: float = 0.0
        net_total_recv: float = 0.0
        net_total_sent: float = 0.0
        net_ping: int = 0.0
        errors_per_sec:int  = 0.0
        drops_per_sec:int  = 0.0
        total_errors:int  = 0.0
        total_drops:int  = 0.0