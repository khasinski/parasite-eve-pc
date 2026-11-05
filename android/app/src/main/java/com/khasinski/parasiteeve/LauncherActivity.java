package com.khasinski.parasiteeve;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Locale;

/** Copies user-selected disc images into private storage; never requests broad storage access. */
public class LauncherActivity extends Activity {
    private static final int PICK_FOLDER = 1;
    private TextView status;
    private ProgressBar progress;
    private Button importButton;
    private Button playButton;
    private boolean importing;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        int padding = (int)(24 * getResources().getDisplayMetrics().density);
        layout.setPadding(padding, padding, padding, padding);
        TextView title = new TextView(this);
        title.setText("Parasite Eve");
        title.setTextSize(28);
        layout.addView(title);
        status = new TextView(this);
        layout.addView(status);
        progress = new ProgressBar(this);
        progress.setVisibility(android.view.View.GONE);
        layout.addView(progress);
        importButton = new Button(this);
        importButton.setText("Import disc images");
        importButton.setOnClickListener(v -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            startActivityForResult(intent, PICK_FOLDER);
        });
        layout.addView(importButton);
        playButton = new Button(this);
        playButton.setText("Play");
        playButton.setOnClickListener(v -> chooseDisc());
        layout.addView(playButton);
        setContentView(layout);
        refresh();
    }

    private File[] cues() {
        File[] files = new File(getFilesDir(), "discs").listFiles(
            f -> f.isFile() && f.getName().toLowerCase(Locale.ROOT).endsWith(".cue"));
        if (files == null) return new File[0];
        Arrays.sort(files);
        return files;
    }

    private void refresh() {
        if (importing) return;
        int count = cues().length;
        status.setText(count == 0
            ? "Select a folder containing your CUE and BIN files. You can import the other disc later. A controller is currently required."
            : count + " CUE image(s) imported. A controller is currently required.");
        playButton.setEnabled(count != 0);
    }

    private void chooseDisc() {
        File[] files = cues();
        if (files.length == 0) return;
        if (files.length == 1) { launch(files[0]); return; }
        String[] names = new String[files.length];
        for (int i = 0; i < files.length; ++i) names[i] = files[i].getName();
        new AlertDialog.Builder(this).setTitle("Start with disc")
            .setItems(names, (dialog, which) -> launch(files[which])).show();
    }

    private void launch(File cue) {
        Intent intent = new Intent(this, GameActivity.class);
        intent.putExtra("disc", cue.getName());
        startActivity(intent);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_FOLDER || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri tree = data.getData();
        importing = true;
        importButton.setEnabled(false);
        playButton.setEnabled(false);
        progress.setVisibility(android.view.View.VISIBLE);
        status.setText("Importing images… Keep the app open.");
        // Keep the Activity alive through rotation while the import owns its UI.
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LOCKED);
        new Thread(() -> {
            String error = null;
            File staging = new File(getFilesDir(), "disc-import");
            ArrayList<File> installed = new ArrayList<>();
            try {
                if (!staging.exists() && !staging.mkdirs()) throw new java.io.IOException("Cannot create import directory");
                ArrayList<File> copied = new ArrayList<>();
                String rootId = DocumentsContract.getTreeDocumentId(tree);
                Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, rootId);
                String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME};
                try (Cursor cursor = getContentResolver().query(children, columns, null, null, null)) {
                    if (cursor == null) throw new java.io.IOException("Cannot read the selected folder");
                    while (cursor.moveToNext()) {
                        String name = cursor.getString(1);
                        if (name == null || name.contains("/") || name.contains("\\") || name.equals(".") || name.equals("..")) continue;
                        String lower = name.toLowerCase(Locale.ROOT);
                        if (!lower.endsWith(".cue") && !lower.endsWith(".bin")) continue;
                        if (new File(new File(getFilesDir(), "discs"), name).exists())
                            throw new java.io.IOException("An image named " + name + " is already imported. Existing images were kept.");
                        Uri source = DocumentsContract.buildDocumentUriUsingTree(tree, cursor.getString(0));
                        File target = new File(staging, name);
                        runOnUiThread(() -> status.setText("Importing " + name + "…"));
                        try (InputStream input = getContentResolver().openInputStream(source);
                             FileOutputStream output = new FileOutputStream(target)) {
                            if (input == null) throw new java.io.IOException("Cannot open " + name);
                            byte[] buffer = new byte[1024 * 1024];
                            int size;
                            while ((size = input.read(buffer)) != -1) output.write(buffer, 0, size);
                            output.getFD().sync();
                        }
                        copied.add(target);
                    }
                }
                boolean hasCue = false;
                for (File file : copied) if (file.getName().toLowerCase(Locale.ROOT).endsWith(".cue")) hasCue = true;
                if (!hasCue) throw new java.io.IOException("No CUE image found in this folder");
                File discs = new File(getFilesDir(), "discs");
                if (!discs.exists() && !discs.mkdirs()) throw new java.io.IOException("Cannot create disc directory");
                // Complete every copy before installing images; existing images are never overwritten.
                // Publish BINs before CUEs so an interrupted installation cannot
                // expose a new CUE before its newly imported data files exist.
                copied.sort((a, b) -> Boolean.compare(
                    a.getName().toLowerCase(Locale.ROOT).endsWith(".cue"),
                    b.getName().toLowerCase(Locale.ROOT).endsWith(".cue")));
                for (File file : copied) {
                    File destination = new File(discs, file.getName());
                    if (!file.renameTo(destination))
                        throw new java.io.IOException("Cannot install " + file.getName());
                    installed.add(destination);
                }
            } catch (Exception failure) {
                for (File file : installed) file.delete();
                error = failure.getMessage();
            } finally {
                File[] leftovers = staging.listFiles();
                if (leftovers != null) for (File file : leftovers) file.delete();
                staging.delete();
            }
            final String message = error;
            runOnUiThread(() -> {
                importing = false;
                importButton.setEnabled(true);
                progress.setVisibility(android.view.View.GONE);
                setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
                refresh();
                if (message != null) new AlertDialog.Builder(this).setTitle("Import failed")
                    .setMessage(message).setPositiveButton("OK", null).show();
            });
        }, "disc-import").start();
    }
}
