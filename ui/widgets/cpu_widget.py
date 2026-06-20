from PySide6.QtCore import Qt,QSize,Slot,Signal
from PySide6.QtWidgets import QApplication,QMainWindow,QLabel,QWidget,QVBoxLayout
class CpuWidget(QWidget):
    def __init__(self):
        super().__init__()

        layout = QVBoxLayout(self)

        self.title_label = QLabel(self)
        self.usage_label = QLabel(self)
        self.temp_label = QLabel(self)
        self.cores_label = QLabel(self)
        self.freq_label = QLabel(self)
        
        self.title_label.setText("--==CPU==--")
        layout.addWidget(self.title_label)
        layout.addWidget(self.usage_label)
        layout.addWidget(self.temp_label)
        layout.addWidget(self.cores_label)
        layout.addWidget(self.freq_label)

    def update_cpu(self,usage,temp,cores,freq):
        self.usage_label.setText(f"Load:{usage:.1f}%")
        self.temp_label.setText(f"Temp:{temp}°C")
        self.cores_label.setText(f"Cores:{cores}")
        self.freq_label.setText(f"Freq:{freq:.1f}Mhz")