from PySide6.QtCore import Qt,QSize,Slot,Signal
from PySide6.QtWidgets import QLabel,QFrame,QVBoxLayout,QScrollArea



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

        self.title_label.setText("CPU")
        self.title_label.setObjectName("Header")

        self.cores_label.setWordWrap(True)
        self.cores_label.setAlignment(Qt.AlignTop)

        self.scroll_area = QScrollArea(self)
        self.scroll_area.setWidgetResizable(True)
        self.scroll_area.setWidget(self.cores_label)
        self.scroll_area.setStyleSheet("""
            QScrollArea { border: none; background-color: transparent; }
            QScrollBar:vertical { width: 10px; background: #1e1e1e; }
            QScrollBar::handle:vertical { background: #ff8c00; border-radius: 5px; } 
        """)

        layout.addWidget(self.title_label)
        layout.addWidget(self.name_label)
        layout.addWidget(self.usage_label)
        layout.addWidget(self.temp_label)
        layout.addWidget(self.freq_label)
        layout.addWidget(self.scroll_area)

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
        