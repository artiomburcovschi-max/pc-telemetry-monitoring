from PySide6.QtCore import Qt,QSize,Slot,Signal
from PySide6.QtWidgets import QApplication,QMainWindow,QLabel
from core.telemetry import TelemetryThread

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Monitoring")
        self.setMinimumSize(QSize(400,200))
        self.setMaximumSize(QSize(700,400))

        self.os_label = QLabel(self)
        self.os_label.setAlignment(Qt.AlignmentFlag.AlignTop)
        self.os_label.setContentsMargins(20,0,0,0)
        self.os_label.setObjectName("Platform")
        self.setStyleSheet("""
        QLabel#Platform 
        {
            color: red;
            font-size:24px;
            letter-spacing:2px;
        }
        """)
        

        self.label = QLabel(self)
        self.setCentralWidget(self.label)
        self.label.setAlignment(Qt.AlignmentFlag.AlignCenter)

        self.telemetry_thread = TelemetryThread()
        self.telemetry_thread.my_signal.connect(self.update_ui)
        self.telemetry_thread.start()

    def update_ui(self,data):
        response = (
            f"CPU: {data.cpu_usage:.1f}%\n"
            f"Cores: {data.cpu_cores}%\n"
            f"Freq: {data.cpu_freq:.1f} MHz\n"
            f"Temp: {data.cpu_temp:.0f}°C\n\n"
            f"RAM: {data.ram_usage:.1f}%\n"
            f"Total: {data.ram_total:.0f} GB\n\n"
            f"GPU: {data.gpu_name}\n"
            f"GPU Load: {data.gpu_usage:.1f}%\n"
            f"GPU Temp: {f'{data.gpu_temp:.0f}' if data.gpu_temp is not None else 'N/A'}°C\n"
            f"VRAM: {data.gpu_c_memory_perc:.1f}%\n"
            f"VRAM Used: {data.gpu_c_memory:.2f} / {data.gpu_memoryTot:.1f} GB"
        )

        self.os_label.setText(data.os)
        self.label.setText(response)
    def closeEvent(self, event):
        self.telemetry_thread.stop()
        event.accept()


