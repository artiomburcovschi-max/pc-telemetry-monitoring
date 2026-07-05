from PySide6.QtCore import Qt,QSize,Slot,Signal
from PySide6.QtWidgets import QLabel,QFrame,QVBoxLayout



class NetWidget(QFrame):
    def __init__(self):
        super().__init__()

        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignTop)
        
        self.title_label = QLabel(self)
        self.download_speed = QLabel(self)
        self.upload_speed = QLabel(self)
        self.total_recv = QLabel(self)
        self.total_sent = QLabel(self)
        self.ping_label = QLabel(self)
        self.errors_label = QLabel(self)
        self.drops_label = QLabel(self)

        self.title_label.setText("NET")
        self.title_label.setObjectName("Header")

        layout.addWidget(self.title_label)
        layout.addWidget(self.download_speed)
        layout.addWidget(self.upload_speed)
        layout.addWidget(self.total_recv)
        layout.addWidget(self.total_sent)
        layout.addWidget(self.ping_label)
        layout.addWidget(self.errors_label)
        layout.addWidget(self.drops_label)

    def update_net(self,download_speed,upload_speed,total_recv,total_sent,ping,errors,tot_errors,drops,tot_drops):
        self.download_speed.setText(f"Download speed:{download_speed} MB/s")
        self.upload_speed.setText(f"Upload speed:{upload_speed} MB/s")
        self.total_recv.setText(f"Total recieved:{total_recv} GB")
        self.total_sent.setText(f"Total sent:{total_sent} GB")
        self.ping_label.setText(f"Ping: {ping} ms")
        self.errors_label.setText(f"Errors: {errors}   ({tot_errors})")
        self.drops_label.setText(f"Drops: {drops}   ({tot_drops})")
