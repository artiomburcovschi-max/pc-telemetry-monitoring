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
        self.disk_info = QLabel(self)
        
        
        self.title_label.setText("MAIN INFORMATION")
        self.title_label.setObjectName("Header")

        layout.addWidget(self.title_label)
        layout.addWidget(self.cpu_usage_label)
        layout.addWidget(self.cpu_temp_label)
        layout.addWidget(self.gpu_usage_label)
        layout.addWidget(self.gpu_temp_label)
        layout.addWidget(self.gpu_c_mem_label)
        layout.addWidget(self.ram_usage)
        layout.addWidget(self.disk_info)

    def update_main(self,cpu_usage,cpu_temp,gpu_usage,gpu_temp,gpu_c_m,ram_usage,disk_info):
        self.cpu_usage_label.setText(f"CPU usage:{cpu_usage:.1f}%")
        self.cpu_temp_label.setText(f"CPU temperature:{cpu_temp}°C")
        self.gpu_usage_label.setText(f"GPU usage:{gpu_usage:.1f}%")
        self.gpu_temp_label.setText(f"GPU temperature:{gpu_temp}°C")
        self.gpu_c_mem_label.setText(f"GPU current memory usage:{gpu_c_m} GB")
        self.ram_usage.setText(f"RAM usage:{ram_usage:.1f}%")
        self.disk_info.setText(f"DISK info:{disk_info}")

