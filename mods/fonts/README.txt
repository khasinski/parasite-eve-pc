Fonts for the game. Any .png here shows up in F1 > Font, named after the file (_ reads as a space).
Original, the game's own letters, is always there too.

Making a font
- PNG, 252 or 256 pixels wide, at least 48 tall, on a see-through background.
- 12 x 12 pixel cells, 21 cells a row, in the game's order:
    0 1 2 3 4 5 6 7 8 9 + - (empty) (empty) % (empty for space) A B C D E
    F G H I J K L M N O P Q R S T U V W X Y Z
    & ! ? " ' . a b c d e f g h i j k l m n o
    p q r s t u v w x y z : , /
- Light pixels are the letter, dark pixels its shadow.
- Each letter's width is measured from its pixels, one past the last filled column.
- To set a width by hand, put one pure magenta pixel (255, 0, 255) in the cell: the width becomes
  that pixel's column + 1, counting from 1. It is never drawn.
- A cell left empty keeps the game's own letter.

Pixel Sans
- Pixel_Sans.png is Pixeloid Sans Bold by GGBotNet (https://ggbot.itch.io/pixeloid-font,
  version 1.0), drawn at its own 9 pixel size, standing on row 8 of each cell, with a dark shadow one
  pixel right and down. Copyright (c) 2020-2025 GGBotNet, with Reserved Font Name "Pixeloid".
- It is licensed under the SIL Open Font License, Version 1.1; the full text is in OFL.txt beside it.
  As the licence asks of a changed font, this one goes by another name, not "Pixeloid".

Departure Mono
- Departure_Mono.png is Departure Mono by Helena Zhang (https://departuremono.com, version 1.500),
  drawn at 11 pixels with tools/fonts/font_sheet.py (threshold 115) and the same one-pixel shadow.
  Copyright 2022-2024 Helena Zhang. Licensed under the SIL Open Font License, Version 1.1; the
  full text is in OFL-DepartureMono.txt beside it. The font has no Reserved Font Name.

Plain Sans
- Plain_Sans.png is IBM Plex Sans Regular (https://github.com/IBM/plex), drawn at 11 pixels with
  tools/fonts/font_sheet.py (threshold 80) and the same one-pixel shadow. Copyright 2017 IBM Corp.,
  with Reserved Font Name "Plex". Licensed under the SIL Open Font License, Version 1.1; the full
  text is in OFL-IBMPlex.txt beside it. As a changed font it goes by another name, not "Plex".
