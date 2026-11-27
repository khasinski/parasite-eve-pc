# Parasite Eve Translation Editor

Extract the whole archive. On Windows, open **Parasite Eve Translation Editor.exe** inside the app folder. On macOS, open **Parasite Eve Translation Editor.app**. On Linux, launch **Parasite Eve Translation Editor** inside the app folder.

## Start

Choose **Try the opera demo**, **Open an existing project**, or **Start a new translation**. The demo shows a translation with two dialogue boxes and uses the bundled font. You can try it without a disc.

## Edit

Search for a place such as Carnegie Hall or the Statue of Liberty, or pick a named scene from the dropdown. To find a line by its words, load a USA disc and choose **Find original dialogue**. Pick a result and select **Go to line**. Then pick a line in the sidebar and type in **Your translation**. The preview updates as you type. Use **Next box** and **Previous box** to see what the player reads. To create a box break, put the cursor between phrases and choose **Start next dialogue box**. Press Enter to start another line in the same box. Keep original game tags such as `{name}` and `{wait}`.

Select **Load original dialogue** and choose a USA Disc 1 or Disc 2 BIN or CUE to display the original English. The disc and original dialogue stay on your computer.

**Save project** creates an editable `.pepack` the first time, then saves to that same file. **Project → Project settings** lets you change the language name, code, or custom CJK font.

## Translate with AI

Open **Translate with AI**. Choose **Export this scene** or **Export all dialogue**. Copy the prompt and give it and the CSV to the AI tool you choose. The prompt uses the language code from **Project settings**. Save the returned CSV, then select **Import translated CSV** in the same dialog. The editor checks the line IDs, English source, and game tags before applying any rows. Blank translations keep your current edits. A room marked **shared dialogue** shows its whole message bank because the game may select the line at runtime. All-dialogue export includes each unique line ID once.

## Track progress and review

The scene picker shows how many lines have translations and how many still need review. Under **Scene dialogue**, choose **Untranslated** to find blanks or **Needs review** to work through AI imports. Imported changes start in the review queue. Read the translation, check the preview, and select **Mark reviewed** when you are happy with the line. Save the project to keep the review status. Shared dialogue uses the same status in every scene where the line appears.

## Share

Choose **Share or export** and pick the result you need:

- **Share only my fixes** creates a small `.pefix` file of changes. Someone with the same starting project can use **Project → Apply someone's fixes**.
- **Export for playtesting** creates a `.pelang` the game can load. Put it in the game's `lang` folder, enable the localization mod, and select the language under **F1 → Language**.
- **Save an editable copy** creates a complete project someone else can open and edit.

The download includes all three sample files. The demo font covers the 21 custom glyphs in the sample. Choose a CJK TTF, OTF, or TTC in **Project settings** for other characters. See FONT-LICENSE.txt and FONT-SOURCE.md for the demo font's license and source.

Portions of this software are copyright © 2024 The FreeType Project (www.freetype.org). All rights reserved.
