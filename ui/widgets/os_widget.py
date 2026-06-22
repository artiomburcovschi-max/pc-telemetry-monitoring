from PySide6.QtCore import Qt,QSize,Slot,Signal
from PySide6.QtWidgets import QApplication,QMainWindow,QLabel,QWidget,QVBoxLayout
class OsWidget(QWidget):
    def __init__(self):
        super().__init__()

        layout = QVBoxLayout(self)

        self.title_label = QLabel(self)
        self.name_label = QLabel(self)

        
        
        self.title_label.setText("--==OS==--")
        layout.addWidget(self.title_label)
        layout.addWidget(self.name_label)

    def update_os(self,name):
        self.name_label.setText(f"OS:{name}")