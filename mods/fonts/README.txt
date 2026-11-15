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
