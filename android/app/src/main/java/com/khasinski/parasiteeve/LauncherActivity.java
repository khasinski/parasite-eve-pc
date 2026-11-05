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
import java.lang.ref.WeakReference;
import java.util.concurrent.atomic.AtomicBoolean;

/** Copies user-selected disc images into private storage; never requests broad storage access. */
public class LauncherActivity extends Activity {
    private static final int PICK_FOLDER = 1;
    private TextView status;
    private ProgressBar progress;
    private Button importButton;
    private Button playButton;
    private static final AtomicBoolean importing = new AtomicBoolean();
    private static WeakReference<LauncherActivity> current = new WeakReference<>(null);
    private static volatile String importStatus = "Importing images… Keep the app open.";
    private static volatile String importError;

    @Override protected void onResume() {
        super.onResume();
        current = new WeakReference<>(this);
        if (status != null) { refresh(); showImportError(); }
    }

    private void showImportError() {
        if (importing.get() || importError == null) return;
        String message = importError;
        importError = null;
        new AlertDialog.Builder(this).setTitle("Import failed")
            .setMessage(message).setPositiveButton("OK", null).show();
    }

    private void notifyImport(String error) {
        // Keep a failure until a live launcher can report it, even if all
        // launcher windows were closed before the background copy completed.
        if (error != null) importError = error;
        runOnUiThread(() -> {
            LauncherActivity activity = current.get();
            if (activity == null || activity.isFinishing() || activity.isDestroyed()) return;
            activity.refresh();
            activity.showImportError();
        });
    }
    private int requiredDisc;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        requiredDisc = getIntent().getIntExtra("requiredDisc", 0);
        if (requiredDisc < 1 || requiredDisc > 2) requiredDisc = 0;
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
        playButton.setText(requiredDisc == 0 ? "Play" : "Choose image");
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
        boolean busy = importing.get();
        importButton.setEnabled(!busy);
        progress.setVisibility(busy ? android.view.View.VISIBLE : android.view.View.GONE);
        setRequestedOrientation(busy ? android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LOCKED
                                    : android.content.pm.ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
        int count = cues().length;
        playButton.setEnabled(!busy && count != 0);
        if (busy) { status.setText(importStatus); return; }
        status.setText(requiredDisc != 0
            ? "Parasite Eve (USA) Disc " + requiredDisc + " required. Import its CUE/BIN folder, then choose its CUE image. Your game is waiting."
            : count == 0
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
        new AlertDialog.Builder(this).setTitle(requiredDisc == 0 ? "Start with disc" : "Choose Disc " + requiredDisc)
            .setItems(names, (dialog, which) -> launch(files[which])).show();
    }

    private void launch(File cue) {
        if (requiredDisc != 0) {
            setResult(RESULT_OK, new Intent().putExtra("disc", cue.getName()));
            finish();
            return;
        }
        Intent intent = new Intent(this, GameActivity.class);
        intent.putExtra("disc", cue.getName());
        startActivity(intent);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_FOLDER || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri tree = data.getData();
        // A picker can return after another launcher was opened. Serialize the
        // entire transaction, including staging cleanup and BIN/CUE publication.
        if (!importing.compareAndSet(false, true)) { refresh(); return; }
        importError = null;
        importStatus = "Importing images… Keep the app open.";
        refresh();
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
                        importStatus = "Importing " + name + "…";
                        notifyImport(null);
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
            importing.set(false);
            // The original Activity may have been closed with Back. Notify the
            // live launcher rather than showing a dialog on a destroyed window.
            notifyImport(error);
        }, "disc-import").start();
    }
}
