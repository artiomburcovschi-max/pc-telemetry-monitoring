from settings.palette import Palette

class Config:
    MIN_WIDTH = 730
    MIN_HEIGHT = 800
    MAX_WIDTH = 830
    MAX_HEIGHT = 900

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
            color: {Palette.ACCENT_ORANGE};
            font-size: 15px;
            font-weight: bold;
        }}
        MainWidget QLabel {{
            font-size: 25px; 
            padding: 8px 0px;
        }}
        MainWidget QLabel#Header {{
            font-size: 30px;
            font-weight: bold;
            color: {Palette.ACCENT_ORANGE}; 
            margin-bottom: 50px; 
        }}
        NetWidget QLabel {{
            font-size: 22px; 
            padding: 8px 0px;
        }}
        NetWidget QLabel#Header {{
            font-size: 40px;
            font-weight: bold;
            color: {Palette.ACCENT_ORANGE}; 
            margin-bottom: 50px; 
        }}
        QTableWidget {{
            background-color: {Palette.BG_PANEL};
            padding: 0px;
            margin: 0px;
            border: 1px solid {Palette.TEXT_MUTED};
        }}
        QHeaderView::section {{
            background-color: {Palette.BG_MAIN};
            color: {Palette.TEXT_MAIN};
            padding: 0px;
            font-family: 'Consolas', monospace;
            font-size: 12px;
            border: 1px solid {Palette.TEXT_MUTED};
        }}
    """