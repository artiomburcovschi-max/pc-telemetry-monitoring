from PySide6.QtCore import Qt,QSize,Slot,Signal
from PySide6.QtWidgets import QApplication,QMainWindow,QLabel,QFrame,QVBoxLayout

from settings.config import Config

class GpuWidget(QFrame):
    def __init__(self):
        super().__init__()

        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignTop)
        
        self.title_label = QLabel(self)
        self.name_label = QLabel(self)
        self.tot_mem_label = QLabel(self)
        self.usage_label = QLabel(self)
        self.temp_label = QLabel(self)
        self.current_mem_label = QLabel(self)
        self.current_p_mem_label = QLabel(self)
        
        
        self.title_label.setText("--==GPU==--")
        self.title_label.setObjectName("Header")

        layout.addWidget(self.title_label)
        layout.addWidget(self.name_label)
        layout.addWidget(self.tot_mem_label)
        layout.addWidget(self.usage_label)
        layout.addWidget(self.temp_label)
        layout.addWidget(self.current_mem_label)
        layout.addWidget(self.current_p_mem_label)

    def update_gpu(self,name,tot,usage,temp,current_mem,current_p_mem):
        self.name_label.setText(f"Name: {name} - {tot}GB")
        self.usage_label.setText(f"Usage:{usage}%")
        self.temp_label.setText(f"Temp:{temp}°C")
        self.current_mem_label.setText(f"Current usage memory:{current_mem}mb")
        self.current_p_mem_label.setText(f"percentage ratio:{current_p_mem:.1f}%")