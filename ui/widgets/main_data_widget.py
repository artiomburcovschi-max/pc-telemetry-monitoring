from PySide6.QtCore import Qt,QSize,Slot,Signal
from PySide6.QtWidgets import QLabel,QFrame,QVBoxLayout


class MainWidget(QFrame):
    def __init__(self):
        super().__init__()

        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignTop)
        
        self.title_label = QLabel(self)
        self.cpu_usage_label = QLabel(self)
        self.cpu_temp_label = QLabel(self)
        self.gpu_usage_label = QLabel(self)
        self.gpu_temp_label = QLabel(self)
        self.gpu_c_mem_label = QLabel(self)
        self.ram_usage = QLabel(self)
        self.disk_speed = QLabel(self)
        self.download_net_speed = QLabel(self)
        self.upload_net_speed = QLabel(self)

        self.title_label.setText("MAIN INFORMATION")
        self.title_label.setObjectName("Header")

        layout.addWidget(self.title_label)
        layout.addWidget(self.cpu_usage_label)
        layout.addWidget(self.cpu_temp_label)
        layout.addWidget(self.gpu_usage_label)
        layout.addWidget(self.gpu_temp_label)
        layout.addWidget(self.gpu_c_mem_label)
        layout.addWidget(self.ram_usage)
        layout.addWidget(self.disk_speed)
        layout.addWidget(self.download_net_speed)
        layout.addWidget(self.upload_net_speed)


    def update_main(self,data):
        self.cpu_usage_label.setText(f"CPU usage:{data.cpu_usage:.1f}%")
        self.cpu_temp_label.setText(f"CPU temperature:{data.cpu_temp}°C")
        self.gpu_usage_label.setText(f"GPU usage:{data.gpu_usage:.1f}%")
        self.gpu_temp_label.setText(f"GPU temperature:{data.gpu_temp}°C")
        self.gpu_c_mem_label.setText(f"GPU current memory usage:{data.gpu_c_memory} GB")
        self.ram_usage.setText(f"RAM usage:{data.ram_usage:.1f}%")
        if data.disk_info:
            main_disk = data.disk_info[0]
            self.disk_speed.setText(f"DISK ({main_disk['type']}):\n Read speed: {main_disk['read_speed']}MB/s     (tot:{main_disk['read_bytes']} GB)\n Write speed:{main_disk['write_speed']}MB/s  (tot:{main_disk['write_bytes']} GB)")
        else:
            self.disk_speed.setText("N/A")
        self.download_net_speed.setText(f"Download net speed:{data.net_download_speed} MB/s")
        self.upload_net_speed.setText(f"Upload net speed:{data.net_upload_speed} MB/s")
