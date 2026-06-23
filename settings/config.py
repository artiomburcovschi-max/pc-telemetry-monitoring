from settings.palette import Palette

class Config:
    MIN_WIDTH = 650
    MIN_HEIGHT = 500
    MAX_WIDTH = 750
    MAX_HEIGHT = 700

    style = f"""
        QMainWindow {{
            background-color: {Palette.BG_MAIN};
        }}
        QFrame {{
            background-color: {Palette.BG_PANEL};
            border: 1px solid {Palette.TEXT_MUTED};/*рамка*/
            border-radius: 5px;       /*закругление*/            
            padding: 5px;             /*отступ между строками*/                       
        }}
        QLabel {{
            color: {Palette.TEXT_MAIN};
            font-family: 'Consolas', monospace;
            font-size: 15px;
            border: none; 
            background: transparent;
        }}
        QLabel#Header {{
            color: {Palette.ACCENT_GREEN};
            font-size: 15px;
            font-weight: bold;
        }}
    """