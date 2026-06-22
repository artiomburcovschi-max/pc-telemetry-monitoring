from PySide6.QtCore import Qt,QSize,Slot,Signal
from PySide6.QtWidgets import QApplication,QMainWindow,QLabel,QWidget,QVBoxLayout
from core.telemetry import TelemetryThread
from ui.widgets.cpu_widget import CpuWidget
from ui.widgets.ram_widget import RamWidget
from ui.widgets.os_widget import OsWidget
class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Monitoring")
        self.setMinimumSize(QSize(400,200))
        self.setMaximumSize(QSize(700,400))

        container = QWidget()
        self.setCentralWidget(container)
        layout = QVBoxLayout(container)

        self.cpu_panel = CpuWidget()
        layout.addWidget(self.cpu_panel)

        self.ram_panel = RamWidget()
        layout.addWidget(self.ram_panel)

        self.os_panel = OsWidget()
        layout.addWidget(self.os_panel)

        self.telemetry_thread = TelemetryThread()
        self.telemetry_thread.my_signal.connect(self.update_ui)
        self.telemetry_thread.start()

    def update_ui(self,data):
        self.cpu_panel.update_cpu(data.cpu_usage,data.cpu_temp,data.cpu_cores,data.cpu_freq)
        self.ram_panel.update_ram(data.ram_usage,data.ram_total)
        self.os_panel.update_os(data.os_name)
    def closeEvent(self, event):
        self.telemetry_thread.stop()
        event.accept()


