from PySide6.QtCore import Qt,QSize,Slot,Signal
from PySide6.QtWidgets import QApplication,QMainWindow,QLabel,QWidget,QVBoxLayout
class RamWidget(QWidget):
    def __init__(self):
        super().__init__()

        layout = QVBoxLayout(self)

        self.title_label = QLabel(self)
        self.usage_label = QLabel(self)
        self.total_label = QLabel(self)
        
        
        self.title_label.setText("--==RAM==--")
        layout.addWidget(self.title_label)
        layout.addWidget(self.usage_label)
        layout.addWidget(self.total_label)

    def update_ram(self,usage,total):
        self.usage_label.setText(f"Load:{usage:.1f}%")
        self.total_label.setText(f"Temp:{total:.1f}GB")