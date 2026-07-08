from PySide6.QtCore import Qt,QSize,Slot,Signal,QProcess
from PySide6.QtWidgets import QLabel, QFrame, QHBoxLayout, QVBoxLayout,QSizePolicy, QPushButton, QTableWidget, QWidget, QTableWidgetItem
from PySide6.QtWidgets import QHeaderView
from collections import deque
import pyqtgraph as pg

class NetWidget(QFrame):
    def __init__(self):
        super().__init__()

        self.layout = QHBoxLayout(self)
        self.layout.setContentsMargins(0, 0, 0, 0)

        left_layout = QVBoxLayout()
        self.current_state = QFrame()
        
        graphic_layout = QVBoxLayout()
        graphic_layout.setContentsMargins(5, 5, 5, 5)

        self.graph_widget = pg.PlotWidget()
        self.curve1 = self.graph_widget.plot(pen='g')
        self.curve2 = self.graph_widget.plot(pen='r')
        graphic_layout.addWidget(self.graph_widget)


        stats_layout = QVBoxLayout(self.current_state)
        
        self.title_label = QLabel(self)
        self.title_label.setText("NET")
        self.title_label.setObjectName("Header")
        left_layout.addWidget(self.title_label)
        left_layout.addWidget(self.graph_widget)

        self.download_speed = QLabel()
        self.upload_speed = QLabel()
        self.total_recv =  QLabel()
        self.total_sent = QLabel()
        self.ping_label = QLabel()
        self.errors_label = QLabel()
        self.drops_label = QLabel()
        
        stats_layout.addWidget(self.download_speed)
        stats_layout.addWidget(self.upload_speed)
        stats_layout.addWidget(self.total_recv)
        stats_layout.addWidget(self.total_sent)
        stats_layout.addWidget(self.ping_label)
        stats_layout.addWidget(self.errors_label)
        stats_layout.addWidget(self.drops_label)

        left_layout.addWidget(self.current_state)


        right_layout = QVBoxLayout()
        self.scan_button = QPushButton("Сканировать сеть")
        self.scan_button.clicked.connect(self.start_scan)

        self.device_table = QTableWidget(self)
        self.device_table.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.device_table.setFrameStyle(QFrame.NoFrame)
        self.device_table.setColumnCount(4)
        self.device_table.setHorizontalHeaderLabels(["IP", "MAC", "Device", "Ping"])
        header = self.device_table.horizontalHeader()
        numeration = self.device_table.verticalHeader()
        numeration.setVisible(False)
        header.setSectionResizeMode(QHeaderView.Interactive)
        header.setSectionResizeMode(0, QHeaderView.ResizeToContents)
        header.setSectionResizeMode(1, QHeaderView.ResizeToContents)
        header.setSectionResizeMode(2, QHeaderView.Stretch)
        header.setSectionResizeMode(3, QHeaderView.ResizeToContents)
        header.setFixedHeight(35)
        
        right_layout.addWidget(self.scan_button)
        right_layout.addWidget(self.device_table,   stretch = 1)
        

        self.layout.addLayout(left_layout)
        self.layout.addLayout(right_layout)
        self.layout.setStretch(0, 5)
        self.layout.setStretch(1, 3)

        self.my_buffer1 = deque([0.0] * 60, maxlen=60)
        self.my_buffer2 = deque([0.0] * 60, maxlen=60)
    def update_net(self,download_speed,upload_speed,total_recv,total_sent,ping,errors,tot_errors,drops,tot_drops):
        self.download_speed.setText(f"Download speed:{download_speed} MB/s")
        self.my_buffer1.append(download_speed)
        self.upload_speed.setText(f"Upload speed:{upload_speed} MB/s")
        self.my_buffer2.append(upload_speed)
        self.total_recv.setText(f"Total recieved:{total_recv} GB")
        self.total_sent.setText(f"Total sent:{total_sent} GB")
        self.ping_label.setText(f"Ping: {ping} ms")
        self.errors_label.setText(f"Errors: {errors}   ({tot_errors})")
        self.drops_label.setText(f"Drops: {drops}   ({tot_drops})")
        self.curve1.setData(list(self.my_buffer1))
        self.curve2.setData(list(self.my_buffer2))
    def start_scan(self):
        self.device_table.setRowCount(0)
        self.layout.setStretch(0, 2)
        self.layout.setStretch(1, 6)
        self.current_state.setStyleSheet("""
            QLabel {
                font-size: 14px;
                padding: 2px 0px;
            }
        """)
        self.process = QProcess(self)
        prog = "./orion_netscan"
        self.process.start(prog)
        self.process.readyReadStandardOutput.connect(self.read_scan_output)
        
    def read_scan_output(self):
        while self.process.canReadLine():
            line_bytes = self.process.readLine()
            line = line_bytes.data().decode('utf-8').strip()

            
            parts = line.split('|')
            if len(parts) == 4:
                row = self.device_table.rowCount()
                self.device_table.insertRow(row)
                for i, text in enumerate(parts):
                    self.device_table.setItem(row, i, QTableWidgetItem(text))
