from PySide6.QtCore import Qt
from PySide6.QtWidgets import QLabel, QFrame, QVBoxLayout  
class DiskWidget(QFrame):
    def __init__(self):
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignTop)
        
        self.title_label = QLabel(self)
        self.disk_info_label = QLabel(self)
        
        self.title_label.setText("DISK")
        self.title_label.setObjectName("Header")

        layout.addWidget(self.title_label)
        layout.addWidget(self.disk_info_label)

    def update_disk(self, disk_info):
        if not disk_info:
            self.disk_info_label.setText("No disks found")
            return
        text_lines = []
        
        for disk in disk_info:
            disk_string = f"Disk {disk['name']}     ({disk['type']}) \n Total: {disk['total']} GB \n Used: {disk['Usage']} GB  {disk['Percent']}% \n Read speed: {disk['read_speed']} MB/s  Tot:({disk['read_bytes']}) GB\n Write speed: {disk['write_speed']} MB/s  Tot:({disk['write_bytes']}) GB"
            text_lines.append(disk_string)
        
        full_text = "\n".join(text_lines)
        self.disk_info_label.setText(full_text)