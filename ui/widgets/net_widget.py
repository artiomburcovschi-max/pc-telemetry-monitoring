from PySide6.QtCore import Qt,QSize,Slot,Signal,QProcess
from PySide6.QtWidgets import QLabel, QFrame, QHBoxLayout, QVBoxLayout,QSizePolicy, QPushButton, QTableWidget, QWidget, QTableWidgetItem
from PySide6.QtWidgets import QHeaderView


class NetWidget(QFrame):
    def __init__(self):
        super().__init__()

        layout = QHBoxLayout(self)
        
        left_layout = QVBoxLayout()
        self.current_state = QFrame()
        self.graphics = QFrame()
        self.graphics.setObjectName("Graph")

        stats_layout = QVBoxLayout(self.current_state)
        
        self.title_label = QLabel(self)
        self.title_label.setText("NET")
        self.title_label.setObjectName("Header")
        left_layout.addWidget(self.title_label)
        left_layout.addWidget(self.graphics)

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
        header.setSectionResizeMode(QHeaderView.Interactive)
        header.setSectionResizeMode(0, QHeaderView.ResizeToContents)
        header.setSectionResizeMode(1, QHeaderView.ResizeToContents)
        header.setSectionResizeMode(2, QHeaderView.Stretch)
        header.setSectionResizeMode(3, QHeaderView.ResizeToContents)
        header.setFixedHeight(35)
        
        right_layout.addWidget(self.scan_button)
        right_layout.addWidget(self.device_table, 1)
        

        layout.addLayout(left_layout, stretch=5)
        layout.addLayout(right_layout, stretch=4)

    def update_net(self,download_speed,upload_speed,total_recv,total_sent,ping,errors,tot_errors,drops,tot_drops):
        self.download_speed.setText(f"Download speed:{download_speed} MB/s")
        self.upload_speed.setText(f"Upload speed:{upload_speed} MB/s")
        self.total_recv.setText(f"Total recieved:{total_recv} GB")
        self.total_sent.setText(f"Total sent:{total_sent} GB")
        self.ping_label.setText(f"Ping: {ping} ms")
        self.errors_label.setText(f"Errors: {errors}   ({tot_errors})")
        self.drops_label.setText(f"Drops: {drops}   ({tot_drops})")

    def start_scan(self):
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