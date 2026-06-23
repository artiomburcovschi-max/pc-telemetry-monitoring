from PySide6.QtCore import Qt,QSize,Slot,Signal
from PySide6.QtWidgets import QApplication,QMainWindow,QLabel,QFrame,QVBoxLayout

from settings.config import Config

class CpuWidget(QFrame):
    def __init__(self):
        super().__init__()

        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignTop)
        
        self.title_label = QLabel(self)
        self.usage_label = QLabel(self)
        self.temp_label = QLabel(self)
        self.cores_label = QLabel(self)
        self.freq_label = QLabel(self)
        self.name_label = QLabel(self)

        self.title_label.setText("--==CPU==--")
        self.title_label.setObjectName("Header")

        self.cores_label.setWordWrap(True)

        layout.addWidget(self.title_label)
        layout.addWidget(self.name_label)
        layout.addWidget(self.usage_label)
        layout.addWidget(self.temp_label)
        layout.addWidget(self.freq_label)
        layout.addWidget(self.cores_label)

    def update_cpu(self,name,usage,temp,cores,freq):
        self.name_label.setText(f"Name: {name}")
        self.usage_label.setText(f"Load:{usage:.1f}%")

        if temp is not None:
            self.temp_label.setText(f"Temp:{temp}°C")
        else:
            self.temp_label.setText(f"Temp N/A")
        self.freq_label.setText(f"Freq:{freq:.1f}Mhz")
        if not cores:
            self.cores_label.setText("Cores: All Idle (<1%)")
        else:
            core_lines = []
            for core_num,core_load in cores:
                core_lines.append(f"Core[{core_num}]:{core_load:.1f}%")
            cores_final = "Active Cores:\n" + "\n".join(core_lines)
            self.cores_label.setText(cores_final)
        