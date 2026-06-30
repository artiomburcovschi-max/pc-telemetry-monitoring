from PySide6.QtCore import Qt,QSize,Slot,Signal
from PySide6.QtWidgets import QApplication,QMainWindow,QLabel,QWidget,QVBoxLayout,QHBoxLayout,QGridLayout,QFrame,QTabWidget
from core.telemetry import TelemetryThread
from ui.widgets.cpu_widget import CpuWidget
from ui.widgets.ram_widget import RamWidget
from ui.widgets.os_widget import OsWidget
from ui.widgets.gpu_widget import GpuWidget
from ui.widgets.main_data_widget import MainWidget
from ui.widgets.disk_widget import DiskWidget

from settings.config import Config

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setStyleSheet(Config.style)
        self.setWindowTitle("Monitoring")
        self.setMinimumSize(QSize(Config.MIN_WIDTH,Config.MIN_HEIGHT))
        self.setMaximumSize(QSize(Config.MAX_WIDTH,Config.MAX_HEIGHT))

        tab_widget = QTabWidget()
        self.setCentralWidget(tab_widget)
    
        container_1 = QWidget()
        layout1 = QGridLayout(container_1)

        self.main_data_panel = MainWidget()
        layout1.addWidget(self.main_data_panel,0,0)

        tab_widget.addTab(container_1,"Обзор")

        container_2 = QWidget()
        layout2 = QHBoxLayout(container_2)

        left_column = QVBoxLayout()
        
        self.cpu_panel = CpuWidget()
        self.gpu_panel = GpuWidget()
        
        left_column.addWidget(self.cpu_panel)
        left_column.addWidget(self.gpu_panel)

        right_column = QVBoxLayout()
        
        self.os_panel = OsWidget()
        self.ram_panel = RamWidget()
        self.disk_panel = DiskWidget()
        
        right_column.addWidget(self.os_panel)
        right_column.addWidget(self.ram_panel)
        right_column.addWidget(self.disk_panel)
        
        right_column.addStretch()

        layout2.addLayout(left_column, stretch=5)
        layout2.addLayout(right_column, stretch=4)

        tab_widget.addTab(container_2, "Детали")
        
        self.telemetry_thread = TelemetryThread()
        self.telemetry_thread.my_signal.connect(self.update_ui)
        self.telemetry_thread.start()

    def update_ui(self,data):
        self.cpu_panel.update_cpu(data.cpu_name,data.cpu_usage,data.cpu_temp,data.cpu_cores,data.cpu_freq)
        self.ram_panel.update_ram(data.ram_usage,data.ram_total)
        self.os_panel.update_os(data.os_name)
        self.gpu_panel.update_gpu(data.gpu_name,data.gpu_memoryTot,data.gpu_usage,data.gpu_temp,data.gpu_c_memory,data.gpu_c_memory_perc)
        self.disk_panel.update_disk(data.disk_info)
        self.main_data_panel.update_main(data.cpu_usage,data.cpu_temp,data.gpu_usage,data.gpu_temp,data.gpu_c_memory,data.ram_usage,data.disk_info)

    def closeEvent(self, event):
        self.telemetry_thread.stop()
        event.accept()


