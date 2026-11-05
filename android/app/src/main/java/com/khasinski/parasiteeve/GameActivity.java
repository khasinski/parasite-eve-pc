package com.khasinski.parasiteeve;

import org.libsdl.app.SDLActivity;
import android.content.pm.ApplicationInfo;
import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;

/** Android loader for the original game modules compiled into libmain.so. */
public class GameActivity extends SDLActivity {
    @Override protected String[] getLibraries() { return new String[] { "main" }; }
    @Override protected String[] getArguments() {
        nativeSetenv("PE_CONFIG_HOME", getFilesDir().getAbsolutePath());
        nativeSetenv("HOME", getFilesDir().getAbsolutePath());
        // Diagnostic CLI admission is confined to development APKs. Other
        // apps must not be able to choose dump destinations in player storage.
        boolean development = (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
        String[] arguments = development ? getIntent().getStringArrayExtra("arguments") : null;
        if (arguments != null) {
            for (String argument : arguments) {
                if (argument.equals("--headless") || argument.equals("--headless-gpu")) {
                    nativeSetenv("PSYZ_AUDIO_CLOCK", "video");
                    nativeSetenv("SDL_AUDIO_DRIVER", "dummy");
                }
            }
        }
        if (arguments != null) return arguments;
        String disc = getIntent().getStringExtra("disc");
        File directory = new File(getFilesDir(), "discs");
        if (disc == null || !new File(disc).getName().equals(disc)) return new String[0];
        File selected = new File(directory, disc);
        if (!selected.isFile()) return new String[0];
        ArrayList<String> options = new ArrayList<>();
        options.add("--disc");
        options.add(selected.getAbsolutePath());
        File[] others = directory.listFiles(f -> f.isFile() && f.getName().toLowerCase(java.util.Locale.ROOT).endsWith(".cue") && !f.equals(selected));
        if (others != null && others.length != 0) {
            Arrays.sort(others);
            options.add("--disc2");
            options.add(others[0].getAbsolutePath());
        }
        return options.toArray(new String[0]);
    }
}
