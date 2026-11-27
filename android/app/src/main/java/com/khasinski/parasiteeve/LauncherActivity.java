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
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Locale;
import java.lang.ref.WeakReference;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/** Imports disc images and exports backups through Android document pickers. */
public class LauncherActivity extends Activity {
    private static final int PICK_FOLDER = 1;
    private static final int EXPORT_REPORT = 2;
    private static final int EXPORT_SAVES = 3;
    private static final int IMPORT_SAVES = 4;
    private TextView status;
    private ProgressBar progress;
    private Button importButton;
    private Button playButton;
    private Button reportButton;
    private Button savesButton;
    private Button importSavesButton;
    private static volatile boolean exportingSaves;
    private static final AtomicBoolean importing = new AtomicBoolean();
    private static final AtomicBoolean reporting = new AtomicBoolean();
    private static volatile String reportResult;
    private static WeakReference<LauncherActivity> current = new WeakReference<>(null);
    private static volatile String importStatus = "Importing images… Keep the app open.";
    private static volatile String importError;

    @Override protected void onResume() {
        super.onResume();
        current = new WeakReference<>(this);
        if (status != null) { refresh(); showImportError(); showReportResult(); }
    }

    private void showReportResult() {
        if (reporting.get() || reportResult == null) return;
        String message = reportResult;
        reportResult = null;
        new AlertDialog.Builder(this).setTitle(exportingSaves ? "Export saves" : "Diagnostic report")
            .setMessage(message).setPositiveButton("OK", null).show();
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
        if (requiredDisc == 0) {
            reportButton = new Button(this);
            reportButton.setText("Export diagnostic report");
            reportButton.setOnClickListener(v -> {
                Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("application/zip");
                intent.putExtra(Intent.EXTRA_TITLE, "parasite-eve-report.zip");
                intent.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                startActivityForResult(intent, EXPORT_REPORT);
            });
            layout.addView(reportButton);
            savesButton = new Button(this);
            savesButton.setText("Export saves");
            savesButton.setOnClickListener(v -> {
                Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("application/zip");
                intent.putExtra(Intent.EXTRA_TITLE, "parasite-eve-saves.zip");
                intent.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
                startActivityForResult(intent, EXPORT_SAVES);
            });
            layout.addView(savesButton);
            importSavesButton = new Button(this);
            importSavesButton.setText("Import saves");
            importSavesButton.setOnClickListener(v -> {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                startActivityForResult(intent, IMPORT_SAVES);
            });
            layout.addView(importSavesButton);
        }
        android.widget.ScrollView scroll = new android.widget.ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(layout);
        setContentView(scroll);
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
        boolean busy = importing.get() || reporting.get();
        importButton.setEnabled(!busy);
        if (reportButton != null) reportButton.setEnabled(!busy);
        if (savesButton != null) savesButton.setEnabled(!busy);
        if (importSavesButton != null) importSavesButton.setEnabled(!busy);
        progress.setVisibility(busy ? android.view.View.VISIBLE : android.view.View.GONE);
        setRequestedOrientation(busy ? android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LOCKED
                                    : android.content.pm.ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED);
        int count = cues().length;
        playButton.setEnabled(!busy && count != 0);
        if (busy) { status.setText(reporting.get() ? (exportingSaves ? "Exporting saves…" : "Exporting diagnostic report…") : importStatus); return; }
        status.setText(requiredDisc != 0
            ? "Parasite Eve (USA) Disc " + requiredDisc + " required. Import its CUE/BIN folder, then choose its CUE image. Your game is waiting."
            : count == 0
            ? "Select a folder containing your CUE and BIN files. You can import the other disc later. Touch controls and controllers are supported."
            : count + " CUE image(s) imported. Touch controls and controllers are supported.");
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

    /* Check the original Save_CalcCrc16 representation without editing bytes.
     * All 8192 bytes participate, with the four-byte CRC word treated as zero. */
    private static void validateSave(byte[] bytes, String name) throws java.io.IOException {
        if (bytes.length != 8192 || bytes[0] != 'S' || bytes[1] != 'C')
            throw new java.io.IOException("Not an 8192-byte Parasite Eve save: " + name);
        int crc = 0xFFFF;
        for (int i = 0; i < bytes.length; ++i) {
            crc ^= (i >= 0x1C8C && i < 0x1C90 ? 0 : bytes[i] & 255) << 8;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc << 1) ^ ((crc & 0x8000) != 0 ? 0x1021 : 0);
        }
        int stored = (bytes[0x1C8C] & 255) | ((bytes[0x1C8D] & 255) << 8);
        if (stored != (~crc & 0xFFFF) || bytes[0x1C8E] != 0 || bytes[0x1C8F] != 0)
            throw new java.io.IOException("Save checksum is invalid: " + name);
    }

    private void importSaves(Uri source) {
        if (reporting.get() || !importing.compareAndSet(false, true)) { refresh(); return; }
        importError = null;
        importStatus = "Importing saves… Keep the app open.";
        refresh();
        new Thread(() -> {
            String error = null;
            File archive = null;
            ArrayList<File> staged = new ArrayList<>(), targets = new ArrayList<>(), installed = new ArrayList<>();
            try {
                String name = null;
                try (Cursor cursor = getContentResolver().query(source,
                        new String[]{android.provider.OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                    if (cursor != null && cursor.moveToFirst()) name = cursor.getString(0);
                }
                archive = File.createTempFile("pe-save-import-", ".tmp", getCacheDir());
                try (InputStream input = getContentResolver().openInputStream(source);
                     OutputStream output = new FileOutputStream(archive)) {
                    if (input == null) throw new java.io.IOException("Cannot open the selected file.");
                    byte[] buffer = new byte[65536];
                    int count, total = 0;
                    while ((count = input.read(buffer)) != -1) {
                        total += count;
                        if (total > 8 * 1024 * 1024)
                            throw new java.io.IOException("Save archives must be smaller than 8 MiB.");
                        output.write(buffer, 0, count);
                    }
                }
                java.util.LinkedHashMap<String, byte[]> saves = new java.util.LinkedHashMap<>();
                if (archive.length() == 8192 && name != null && name.matches("BASLUS-00662000000[01][A-O]")) {
                    saves.put("bu00/" + name, java.nio.file.Files.readAllBytes(archive.toPath()));
                } else {
                    try (java.util.zip.ZipFile zip = new java.util.zip.ZipFile(archive)) {
                        java.util.Enumeration<? extends ZipEntry> entries = zip.entries();
                        int count = 0;
                        while (entries.hasMoreElements()) {
                            ZipEntry entry = entries.nextElement();
                            if (++count > 64) throw new java.io.IOException("Too many archive entries.");
                            if (entry.isDirectory() && entry.getName().matches("bu[01]0/")) continue;
                            String path = entry.getName();
                            if (!path.matches("bu[01]0/BASLUS-00662000000[01][A-O]") || entry.getSize() != 8192
                                    || saves.containsKey(path))
                                throw new java.io.IOException("Invalid or duplicate save entry: " + path);
                            byte[] bytes = new byte[8192];
                            try (InputStream input = zip.getInputStream(entry)) {
                                int read = 0, size;
                                while (read < bytes.length && (size = input.read(bytes, read, bytes.length - read)) != -1)
                                    read += size;
                                if (read != bytes.length || input.read() != -1)
                                    throw new java.io.IOException("Incomplete save entry: " + path);
                            }
                            java.util.zip.CRC32 crc = new java.util.zip.CRC32();
                            crc.update(bytes);
                            if (crc.getValue() != entry.getCrc())
                                throw new java.io.IOException("Archive checksum is invalid: " + path);
                            saves.put(path, bytes);
                        }
                    }
                }
                if (saves.isEmpty()) throw new java.io.IOException("No saves found. Select a save ZIP or an original BASLUS-00662 save file.");
                File root = new File(getFilesDir(), "memory-cards");
                if (!root.isDirectory() && !root.mkdirs()) throw new java.io.IOException("Cannot create memory cards.");
                root = root.getCanonicalFile();
                for (java.util.Map.Entry<String, byte[]> save : saves.entrySet()) {
                    validateSave(save.getValue(), save.getKey());
                    File target = new File(root, save.getKey());
                    if (!target.getCanonicalFile().equals(target))
                        throw new java.io.IOException("Save path is a link: " + save.getKey());
                    if (target.exists())
                        throw new java.io.IOException("Save already exists: " + save.getKey() + ". Existing saves were kept.");
                    File temporary = File.createTempFile("pe-save-", ".tmp", root);
                    staged.add(temporary); targets.add(target);
                    try (FileOutputStream output = new FileOutputStream(temporary)) {
                        output.write(save.getValue()); output.getFD().sync();
                    }
                }
                /* Android SELinux disallows hard links in app storage. Use
                 * exclusive creation: even a late collision must never truncate
                 * an existing save. All payloads were validated before this. */
                for (int i = 0; i < staged.size(); ++i) {
                    File target = targets.get(i);
                    if (!target.getParentFile().isDirectory() && !target.getParentFile().mkdirs())
                        throw new java.io.IOException("Cannot create card directory.");
                    try (java.nio.channels.FileChannel output = java.nio.channels.FileChannel.open(target.toPath(),
                            java.nio.file.StandardOpenOption.CREATE_NEW, java.nio.file.StandardOpenOption.WRITE)) {
                        installed.add(target);
                        byte[] bytes = java.nio.file.Files.readAllBytes(staged.get(i).toPath());
                        java.nio.ByteBuffer buffer = java.nio.ByteBuffer.wrap(bytes);
                        while (buffer.hasRemaining()) output.write(buffer);
                        output.force(true);
                    }
                }
                importStatus = saves.size() + " save(s) imported.";
            } catch (Exception failure) {
                error = failure.getMessage() != null ? failure.getMessage() : failure.toString();
                for (File file : installed) file.delete();
            } finally {
                for (File file : staged) file.delete();
                if (archive != null) archive.delete();
            }
            final String success = importStatus;
            importing.set(false);
            final String message = error;
            notifyImport(message);
            if (message == null) runOnUiThread(() -> {
                LauncherActivity activity = current.get();
                if (activity == null || activity.isFinishing() || activity.isDestroyed()) return;
                new AlertDialog.Builder(activity).setTitle("Import saves").setMessage(success)
                    .setPositiveButton("OK", null).show();
            });
        }, "pe-save-import").start();
    }

    private void exportSaves(Uri destination) {
        if (importing.get() || !reporting.compareAndSet(false, true)) { refresh(); return; }
        exportingSaves = true;
        reportResult = null;
        refresh();
        new Thread(() -> {
            String result;
            try {
                File root = new File(getFilesDir(), "memory-cards").getCanonicalFile();
                ArrayList<File> files = new ArrayList<>();
                for (String slot : new String[]{"bu00", "bu10"}) {
                    File directory = new File(root, slot);
                    if (!directory.getCanonicalFile().equals(directory))
                        throw new java.io.IOException("Memory card directory is a link.");
                    File[] saves = directory.listFiles(f -> f.isFile()
                        && f.getName().matches("BASLUS-00662000000[01][A-O]"));
                    if (saves == null) continue;
                    Arrays.sort(saves);
                    for (File save : saves) {
                        if (save.length() != 8192 || !save.getCanonicalFile().equals(save))
                            throw new java.io.IOException("Invalid save: " + save.getName());
                        files.add(save);
                    }
                }
                if (files.isEmpty()) throw new java.io.IOException("No saved games are available yet.");
                try (OutputStream stream = getContentResolver().openOutputStream(destination, "wt")) {
                    if (stream == null) throw new java.io.IOException("Cannot open the selected destination.");
                    try (ZipOutputStream zip = new ZipOutputStream(stream)) {
                        for (File save : files) {
                            byte[] bytes;
                            try (InputStream input = new FileInputStream(save)) {
                                bytes = new byte[8192];
                                int count = 0, size;
                                while (count < bytes.length && (size = input.read(bytes, count, bytes.length - count)) != -1)
                                    count += size;
                                if (count != bytes.length || input.read() != -1)
                                    throw new java.io.IOException("Save changed during export. Try again after closing the game.");
                            }
                            zip.putNextEntry(new ZipEntry(save.getParentFile().getName() + "/" + save.getName()));
                            zip.write(bytes);
                            zip.closeEntry();
                        }
                    }
                }
                result = "Saves exported. The ZIP contains the original save files from both memory cards. Keep it as a backup or extract it for the desktop port or save editor.";
            } catch (Exception error) { result = "Export failed: " + error.getMessage(); }
            reportResult = result;
            reporting.set(false);
            runOnUiThread(() -> {
                LauncherActivity activity = current.get();
                if (activity == null || activity.isFinishing() || activity.isDestroyed()) return;
                activity.refresh(); activity.showReportResult();
            });
        }, "pe-save-export").start();
    }

    private void exportReport(Uri destination) {
        if (importing.get() || !reporting.compareAndSet(false, true)) { refresh(); return; }
        exportingSaves = false;
        reportResult = null;
        refresh();
        new Thread(() -> {
            String result;
            try {
                File root = getFilesDir();
                File logs = new File(root, ".local/state/parasite-eve/logs");
                ArrayList<File> files = new ArrayList<>();
                File diagnostic = new File(root, "diagnostics.log");
                if (diagnostic.isFile() && diagnostic.length() != 0) files.add(diagnostic);
                File currentLog = new File(logs, "parasite-eve-log.txt");
                if (currentLog.isFile() && currentLog.length() != 0) files.add(currentLog);
                File[] crashes = logs.listFiles(f -> f.isFile() && f.length() != 0
                    && f.getName().startsWith("crash-") && f.getName().endsWith(".txt"));
                if (crashes != null) {
                    Arrays.sort(crashes, (left, right) -> {
                        int order = Long.compare(right.lastModified(), left.lastModified());
                        return order != 0 ? order : left.getName().compareTo(right.getName());
                    });
                    for (int i = 0; i < Math.min(5, crashes.length); ++i) files.add(crashes[i]);
                }
                if (files.isEmpty()) throw new java.io.IOException("No game logs are available yet. Start the game first.");
                try (OutputStream stream = getContentResolver().openOutputStream(destination, "wt")) {
                    if (stream == null) throw new java.io.IOException("Cannot open the selected destination.");
                    try (ZipOutputStream zip = new ZipOutputStream(stream)) {
                        zip.putNextEntry(new ZipEntry("system.txt"));
                        String info = "Parasite Eve " + getPackageManager().getPackageInfo(getPackageName(), 0).versionName
                            + "\nAndroid API " + android.os.Build.VERSION.SDK_INT
                            + "\nDevice " + android.os.Build.MANUFACTURER + " " + android.os.Build.MODEL + "\n";
                        zip.write(info.getBytes(java.nio.charset.StandardCharsets.UTF_8));
                        zip.closeEntry();
                        byte[] buffer = new byte[65536];
                        long total = 0;
                        for (File file : files) {
                            zip.putNextEntry(new ZipEntry(file.equals(diagnostic)
                                ? "diagnostics.log" : "logs/" + file.getName()));
                            try (InputStream input = new FileInputStream(file)) {
                                int size;
                                while ((size = input.read(buffer)) != -1) {
                                    total += size;
                                    if (total > 64L * 1024 * 1024)
                                        throw new java.io.IOException("The logs exceed the 64 MiB report limit. The exported file is incomplete.");
                                    zip.write(buffer, 0, size);
                                }
                            }
                            zip.closeEntry();
                        }
                    }
                }
                result = "Report saved. It contains game logs and up to five recent crash reports. Disc images and saves are not included.";
            } catch (Exception error) {
                result = "Export failed: " + error.getMessage();
            }
            reportResult = result;
            reporting.set(false);
            runOnUiThread(() -> {
                LauncherActivity activity = current.get();
                if (activity == null || activity.isFinishing() || activity.isDestroyed()) return;
                activity.refresh();
                activity.showReportResult();
            });
        }, "pe-report-export").start();
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request == IMPORT_SAVES) {
            if (result == RESULT_OK && data != null && data.getData() != null)
                importSaves(data.getData());
            return;
        }
        if (request == EXPORT_SAVES) {
            if (result == RESULT_OK && data != null && data.getData() != null)
                exportSaves(data.getData());
            return;
        }
        if (request == EXPORT_REPORT) {
            if (result == RESULT_OK && data != null && data.getData() != null)
                exportReport(data.getData());
            return;
        }
        if (request != PICK_FOLDER || result != RESULT_OK || data == null || data.getData() == null) return;
        Uri tree = data.getData();
        // A picker can return after another launcher was opened. Serialize the
        // entire transaction, including staging cleanup and BIN/CUE publication.
        if (reporting.get() || !importing.compareAndSet(false, true)) { refresh(); return; }
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
