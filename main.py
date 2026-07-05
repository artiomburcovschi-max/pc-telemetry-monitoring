import sys
from PySide6.QtWidgets import QApplication
from PySide6.QtCore import QTimer
from ui.splash import SplashScreen
from ui.window import MainWindow

if __name__=="__main__":
    app = QApplication(sys.argv)

    splash = SplashScreen()
    splash.show()

    window = MainWindow()

    def show_main():
        window.show()
        splash.finish(window)

    QTimer.singleShot(2000, show_main)

    sys.exit(app.exec())