from PySide6.QtWidgets import QSplashScreen
from PySide6.QtGui import QPixmap, QColor, QFont
from PySide6.QtCore import Qt

class SplashScreen(QSplashScreen):
    def __init__(self):
        pixmap = QPixmap(500, 400)
        pixmap.fill(QColor("#1e1e2e")) 
        super().__init__(pixmap)
        
        self.setFont(QFont("Arial", 16, QFont.Weight.Bold))
        
        self.showMessage("Загрузка...", Qt.AlignCenter, Qt.white)