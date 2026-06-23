from PySide6.QtCore import Qt,QSize,Slot,Signal
from PySide6.QtWidgets import QApplication,QMainWindow,QLabel,QWidget,QGridLayout
from core.telemetry import TelemetryThread
from ui.widgets.cpu_widget import CpuWidget
from ui.widgets.ram_widget import RamWidget
from ui.widgets.os_widget import OsWidget
from ui.widgets.gpu_widget import GpuWidget


from settings.config import Config

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setStyleSheet(Config.style)
        self.setWindowTitle("Monitoring")
        self.setMinimumSize(QSize(Config.MIN_WIDTH,Config.MIN_HEIGHT))
        self.setMaximumSize(QSize(Config.MAX_WIDTH,Config.MAX_HEIGHT))

        container = QWidget()
        self.setCentralWidget(container)
        layout = QGridLayout(container)

        self.cpu_panel = CpuWidget()
        layout.addWidget(self.cpu_panel,0,0)

        self.ram_panel = RamWidget()
        layout.addWidget(self.ram_panel,0,1)

        self.os_panel = OsWidget()
        layout.addWidget(self.os_panel,1,1)

        self.gpu_panel = GpuWidget()
        layout.addWidget(self.gpu_panel,1,0)

        self.telemetry_thread = TelemetryThread()
        self.telemetry_thread.my_signal.connect(self.update_ui)
        self.telemetry_thread.start()

    def update_ui(self,data):
        self.cpu_panel.update_cpu(data.cpu_name,data.cpu_usage,data.cpu_temp,data.cpu_cores,data.cpu_freq)
        self.ram_panel.update_ram(data.ram_usage,data.ram_total)
        self.os_panel.update_os(data.os_name)
        self.gpu_panel.update_gpu(data.gpu_name,data.gpu_memoryTot,data.gpu_usage,data.gpu_temp,data.gpu_c_memory,data.gpu_c_memory_perc)
    def closeEvent(self, event):
        self.telemetry_thread.stop()
        event.accept()


